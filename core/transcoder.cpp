#include "core/transcoder.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <dcmtk/dcmdata/dcdeftag.h>

#include "codec/htj2k_encoder.hpp"
#include "codec/source_decoder.hpp"
#include "core/file_discovery.hpp"
#include "core/job_scheduler.hpp"
#include "core/patient_zipper.hpp"
#include "dicom/dicom_metadata.hpp"
#include "dicom/dicom_reader.hpp"
#include "dicom/dicom_writer.hpp"
#include "dicom/photometric.hpp"
#include "dicom/streaming_pixel_sequence_builder.hpp"
#include "dicom/transfer_syntax.hpp"
#include "util/fs.hpp"
#include "util/logging.hpp"
#include "util/timer.hpp"

namespace htj2k::core {
namespace {

void validate_options(const TranscodeOptions& options)
{
  if (!std::filesystem::exists(options.input_root) || !std::filesystem::is_directory(options.input_root)) {
    throw std::runtime_error("input root does not exist or is not a directory");
  }
  if (options.in_place && !options.output_root.empty()) {
    throw std::runtime_error("in-place mode must not use output-root");
  }
}

bool is_supported_for_transcode(const dicom::ImageSpec& spec)
{
  return spec.has_pixel_data && !spec.is_float && !spec.is_double && spec.bits_allocated != 1 &&
         (spec.bits_allocated == 8 || spec.bits_allocated == 16 || spec.bits_allocated == 32) &&
         (spec.samples_per_pixel == 1 || spec.samples_per_pixel == 3);
}

void install_pixel_sequence(DcmDataset& dataset, dicom::StreamingPixelSequenceResult pixel_sequence)
{
  DcmElement* element = nullptr;
  if (dataset.findAndGetElement(DCM_PixelData, element).bad() || element == nullptr) {
    throw std::runtime_error("PixelData element not found");
  }
  auto* pixel_data = dynamic_cast<DcmPixelData*>(element);
  if (pixel_data == nullptr) {
    throw std::runtime_error("PixelData element has unexpected type");
  }

  const auto frame_count = pixel_sequence.extended_offsets.size();
  if (frame_count != pixel_sequence.extended_lengths.size()) {
    throw std::runtime_error("extended offset table metadata size mismatch");
  }
  pixel_data->putOriginalRepresentation(
    EXS_HighThroughputJPEG2000LosslessOnly, nullptr, pixel_sequence.pixel_sequence.release());
  if (frame_count > 1) {
    dataset.putAndInsertUint64Array(
      DCM_ExtendedOffsetTable, pixel_sequence.extended_offsets.data(), pixel_sequence.extended_offsets.size());
    dataset.putAndInsertUint64Array(
      DCM_ExtendedOffsetTableLengths, pixel_sequence.extended_lengths.data(), pixel_sequence.extended_lengths.size());
  } else {
    dataset.findAndDeleteElement(DCM_ExtendedOffsetTable, true, true);
    dataset.findAndDeleteElement(DCM_ExtendedOffsetTableLengths, true, true);
  }
}

void maybe_log_progress(const RuntimeStats& stats, const std::size_t total)
{
  const auto completed = stats.completed.load();
  std::ostringstream out;
  out << "progress completed=" << completed << "/" << total << " ok=" << stats.ok.load()
      << " copied=" << stats.copied.load() << " failed=" << stats.failed.load();
  util::info(out.str());
}

}  // namespace

Transcoder::Transcoder(BuildInfo build_info, TranscodeOptions options)
    : build_info_(std::move(build_info)), options_(std::move(options))
{
}

TranscodeReport Transcoder::run()
{
  validate_options(options_);
  dicom::register_dcmtk_codecs();

  util::ScopedTimer total_timer;
  util::ScopedTimer discovery_timer;
  auto report = TranscodeReport(options_);
  report.set_build_info(build_info_);

  const auto discovery = discover_files(options_.input_root);
  report.set_discovery(discovery);
  report.finalize_discovery(discovery_timer.elapsed_seconds());

  if (!options_.in_place) {
    for (const auto& directory : discovery.directories) {
      std::filesystem::create_directories(options_.output_root / directory);
    }
  }

  std::ostringstream header;
  header << "input=" << options_.input_root.string() << " output="
         << (options_.in_place ? std::string("in-place") : options_.output_root.string())
         << " files=" << discovery.files.size() << " workers=" << options_.workers << " build="
         << build_info_.build_type << " compiler=" << build_info_.compiler << " arch=" << build_info_.arch;
  util::info(header.str());

  RuntimeStats stats;
  stats.discovered.store(discovery.files.size());

  std::mutex progress_mutex;
  std::condition_variable progress_cv;
  bool stop_progress = false;
  std::thread progress_thread([&] {
    std::unique_lock<std::mutex> lock(progress_mutex);
    for (;;) {
      if (progress_cv.wait_for(lock, std::chrono::seconds(5), [&] { return stop_progress; })) {
        break;
      }
      lock.unlock();
      maybe_log_progress(stats, discovery.files.size());
      lock.lock();
    }
  });
  auto stop_progress_thread = [&] {
    {
      std::lock_guard<std::mutex> lock(progress_mutex);
      stop_progress = true;
    }
    progress_cv.notify_one();
    if (progress_thread.joinable()) {
      progress_thread.join();
    }
  };

  JobScheduler scheduler(options_.workers);
  std::vector<JobResult> results;
  try {
    results = scheduler.run(discovery.files, stats, [this](const std::filesystem::path& relative_path) {
      return process_one(relative_path);
    });
  } catch (...) {
    stop_progress_thread();
    throw;
  }
  stop_progress_thread();

  for (auto& result : results) {
    report.add_result(std::move(result));
  }

  if (options_.zip.enabled) {
    const auto zip_root = options_.in_place ? options_.input_root : options_.output_root;
    for (auto& zip_result : zip_patients(zip_root, options_.zip)) {
      report.add_zip_result(zip_result.patient_key, zip_result.ok, zip_result.message);
    }
  }

  report.finalize(total_timer.elapsed_seconds());
  if (options_.report_json.has_value()) {
    report.write_json(*options_.report_json);
  }
  util::info(report.summary_text());
  return report;
}

JobResult Transcoder::process_one(const std::filesystem::path& relative_path) const
{
  JobResult result;
  result.source_path = source_path_for(relative_path);
  result.destination_path = destination_path_for(relative_path);
  result.patient_key = patient_key_for(relative_path);
  util::ScopedTimer total_timer;

  try {
    if (!options_.in_place && std::filesystem::exists(result.destination_path) && !options_.overwrite) {
      throw std::runtime_error("destination exists and overwrite is disabled");
    }

    util::ScopedTimer timer;
    auto loaded = dicom::load_dicom_file(result.source_path);
    result.bytes_read = loaded.file_size;
    result.phase_times.load_seconds = timer.elapsed_seconds();

    timer.reset();
    auto spec = dicom::extract_image_spec(*loaded.dataset, loaded.source_transfer_syntax);
    spec.photometric_plan =
      dicom::plan_photometric(spec.photometric_interpretation, spec.samples_per_pixel, options_.strict_color);
    spec.target_photometric_interpretation = spec.photometric_plan.target_photometric;
    result.phase_times.metadata_seconds = timer.elapsed_seconds();

    if (!is_supported_for_transcode(spec) || !dicom::is_supported_photometric(spec.photometric_interpretation)) {
      if (options_.strict_color) {
        throw std::runtime_error("dataset is not supported for transcode");
      }
      if (!options_.in_place) {
        util::atomic_copy_file(result.source_path, result.destination_path, options_.overwrite);
        result.bytes_written = static_cast<std::uint64_t>(std::filesystem::file_size(result.destination_path));
      }
      result.status = "copied";
      result.message = "unsupported dataset";
      result.phase_times.total_seconds = total_timer.elapsed_seconds();
      return result;
    }

    std::unique_ptr<codec::ISourceDecoder> decoder;
    try {
      decoder = codec::create_source_decoder(loaded, spec);
    } catch (const std::exception& ex) {
      if (options_.strict_color) {
        throw;
      }
      if (!options_.in_place) {
        util::atomic_copy_file(result.source_path, result.destination_path, options_.overwrite);
        result.bytes_written = static_cast<std::uint64_t>(std::filesystem::file_size(result.destination_path));
      }
      result.status = "copied";
      result.message = ex.what();
      result.phase_times.total_seconds = total_timer.elapsed_seconds();
      return result;
    }

    codec::Htj2kEncoder encoder;
    codec::OwnedFrameBuffer scratch;
    dicom::StreamingPixelSequenceBuilder pixel_sequence_builder;

    timer.reset();
    for (std::size_t frame = 0; frame < decoder->frame_count(); ++frame) {
      const auto frame_view = decoder->decode_frame(frame, scratch);
      result.frames += 1;
      result.pixels += static_cast<std::uint64_t>(spec.rows) * static_cast<std::uint64_t>(spec.columns);

      const auto decode_elapsed = timer.elapsed_seconds();
      result.phase_times.decode_seconds += decode_elapsed;

      timer.reset();
      auto encoded = encoder.encode(spec, frame_view, options_.encode);
      result.phase_times.encode_seconds += timer.elapsed_seconds();
      timer.reset();
      pixel_sequence_builder.append_frame(std::move(encoded.codestream));
      result.phase_times.encapsulate_seconds += timer.elapsed_seconds();
      timer.reset();
    }

    timer.reset();
    install_pixel_sequence(*loaded.dataset, pixel_sequence_builder.finalize());
    result.phase_times.encapsulate_seconds += timer.elapsed_seconds();

    timer.reset();
    dicom::apply_output_metadata(
      *loaded.dataset, spec, dicom::MetadataUpdatePlan{options_.regenerate_sop_instance_uid});
    dicom::write_dicom_file(*loaded.file_format, result.destination_path);
    result.phase_times.write_seconds = timer.elapsed_seconds();

    result.bytes_written = static_cast<std::uint64_t>(std::filesystem::file_size(result.destination_path));
    result.status = "ok";
    result.message = "transcoded";
    result.phase_times.total_seconds = total_timer.elapsed_seconds();
    return result;
  } catch (const std::exception& ex) {
    result.status = "failed";
    result.message = ex.what();
    result.phase_times.total_seconds = total_timer.elapsed_seconds();
    return result;
  }
}

std::filesystem::path Transcoder::source_path_for(const std::filesystem::path& relative_path) const
{
  return options_.input_root / relative_path;
}

std::filesystem::path Transcoder::destination_path_for(const std::filesystem::path& relative_path) const
{
  return options_.in_place ? (options_.input_root / relative_path) : (options_.output_root / relative_path);
}

std::string Transcoder::patient_key_for(const std::filesystem::path& relative_path) const
{
  const auto begin = relative_path.begin();
  if (begin == relative_path.end()) {
    return options_.input_root.filename().string();
  }
  return begin->string();
}

}  // namespace htj2k::core
