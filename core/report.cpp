#include "core/report.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "util/fs.hpp"

namespace htj2k::core {

TranscodeReport::TranscodeReport(TranscodeOptions options) : options_(std::move(options)) {}

void TranscodeReport::set_build_info(BuildInfo build_info) { build_info_ = std::move(build_info); }

void TranscodeReport::set_discovery(const DiscoveryResult& discovery)
{
  discovery_ = discovery;
  summary_.total = discovery.files.size();
}

void TranscodeReport::finalize_discovery(const double seconds) { discovery_seconds_ = seconds; }

void TranscodeReport::add_result(JobResult result)
{
  summary_.frames += result.frames;
  summary_.pixels += result.pixels;
  summary_.bytes_read += result.bytes_read;
  summary_.bytes_written += result.bytes_written;
  phase_totals_ += result.phase_times;

  if (result.status == "ok") {
    ++summary_.ok;
  } else if (result.status == "copied") {
    ++summary_.copied;
  } else if (result.status == "failed") {
    ++summary_.failed;
    ++failure_reasons_[result.message];
  }

  jobs_.push_back(std::move(result));
}

void TranscodeReport::add_zip_result(const std::string& patient_key, const bool ok, const std::string& message)
{
  JobResult result;
  result.patient_key = patient_key;
  result.status = ok ? "zip-ok" : "zip-failed";
  result.message = message;
  jobs_.push_back(std::move(result));
  if (ok) {
    ++summary_.zipped;
  } else {
    ++summary_.failed;
    ++failure_reasons_[message];
  }
}

void TranscodeReport::finalize(const double total_seconds) { total_seconds_ = total_seconds; }

void TranscodeReport::write_json(const std::filesystem::path& path) const
{
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to open report json path: " + path.string());
  }

  out << "{\n";
  out << "  \"summary\": {\n";
  out << "    \"total\": " << summary_.total << ",\n";
  out << "    \"ok\": " << summary_.ok << ",\n";
  out << "    \"copied\": " << summary_.copied << ",\n";
  out << "    \"failed\": " << summary_.failed << ",\n";
  out << "    \"zipped\": " << summary_.zipped << ",\n";
  out << "    \"frames\": " << summary_.frames << ",\n";
  out << "    \"pixels\": " << summary_.pixels << ",\n";
  out << "    \"bytes_read\": " << summary_.bytes_read << ",\n";
  out << "    \"bytes_written\": " << summary_.bytes_written << ",\n";
  out << "    \"discovery_seconds\": " << discovery_seconds_ << ",\n";
  out << "    \"total_seconds\": " << total_seconds_ << "\n";
  out << "  },\n";
  out << "  \"phase_totals\": {\n";
  out << "    \"load_seconds\": " << phase_totals_.load_seconds << ",\n";
  out << "    \"metadata_seconds\": " << phase_totals_.metadata_seconds << ",\n";
  out << "    \"decode_seconds\": " << phase_totals_.decode_seconds << ",\n";
  out << "    \"encode_seconds\": " << phase_totals_.encode_seconds << ",\n";
  out << "    \"encapsulate_seconds\": " << phase_totals_.encapsulate_seconds << ",\n";
  out << "    \"write_seconds\": " << phase_totals_.write_seconds << ",\n";
  out << "    \"total_seconds\": " << phase_totals_.total_seconds << "\n";
  out << "  },\n";
  out << "  \"build\": {\n";
  out << "    \"version\": \"" << util::json_escape(build_info_.version) << "\",\n";
  out << "    \"compiler\": \"" << util::json_escape(build_info_.compiler) << "\",\n";
  out << "    \"build_type\": \"" << util::json_escape(build_info_.build_type) << "\",\n";
  out << "    \"arch\": \"" << util::json_escape(build_info_.arch) << "\"\n";
  out << "  },\n";
  out << "  \"failures\": {\n";
  bool first_reason = true;
  for (const auto& [reason, count] : failure_reasons_) {
    if (!first_reason) {
      out << ",\n";
    }
    first_reason = false;
    out << "    \"" << util::json_escape(reason) << "\": " << count;
  }
  if (!first_reason) {
    out << '\n';
  }
  out << "  },\n";
  out << "  \"jobs\": [\n";
  for (std::size_t i = 0; i < jobs_.size(); ++i) {
    const auto& job = jobs_[i];
    out << "    {\n";
    out << "      \"source\": \"" << util::json_escape(job.source_path.string()) << "\",\n";
    out << "      \"destination\": \"" << util::json_escape(job.destination_path.string()) << "\",\n";
    out << "      \"patient\": \"" << util::json_escape(job.patient_key) << "\",\n";
    out << "      \"status\": \"" << util::json_escape(job.status) << "\",\n";
    out << "      \"message\": \"" << util::json_escape(job.message) << "\",\n";
    out << "      \"frames\": " << job.frames << ",\n";
    out << "      \"pixels\": " << job.pixels << ",\n";
    out << "      \"bytes_read\": " << job.bytes_read << ",\n";
    out << "      \"bytes_written\": " << job.bytes_written << ",\n";
    out << "      \"phase_times\": {\n";
    out << "        \"load_seconds\": " << job.phase_times.load_seconds << ",\n";
    out << "        \"metadata_seconds\": " << job.phase_times.metadata_seconds << ",\n";
    out << "        \"decode_seconds\": " << job.phase_times.decode_seconds << ",\n";
    out << "        \"encode_seconds\": " << job.phase_times.encode_seconds << ",\n";
    out << "        \"encapsulate_seconds\": " << job.phase_times.encapsulate_seconds << ",\n";
    out << "        \"write_seconds\": " << job.phase_times.write_seconds << ",\n";
    out << "        \"total_seconds\": " << job.phase_times.total_seconds << "\n";
    out << "      }\n";
    out << "    }" << (i + 1 == jobs_.size() ? "\n" : ",\n");
  }
  out << "  ]\n";
  out << "}\n";
}

int TranscodeReport::exit_code() const { return summary_.failed == 0 ? 0 : 1; }

std::string TranscodeReport::summary_text() const
{
  std::ostringstream out;
  const double files_per_second = total_seconds_ > 0.0 ? static_cast<double>(summary_.ok + summary_.copied) / total_seconds_ : 0.0;
  const double frames_per_second = total_seconds_ > 0.0 ? static_cast<double>(summary_.frames) / total_seconds_ : 0.0;
  const double mpix_per_second = total_seconds_ > 0.0 ? static_cast<double>(summary_.pixels) / 1'000'000.0 / total_seconds_ : 0.0;
  out << "total=" << summary_.total << " ok=" << summary_.ok << " copied=" << summary_.copied
      << " failed=" << summary_.failed << " zipped=" << summary_.zipped << " files/s=" << files_per_second
      << " frames/s=" << frames_per_second << " MPix/s=" << mpix_per_second;
  return out.str();
}

}  // namespace htj2k::core
