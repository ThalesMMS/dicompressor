#include <iostream>

#include "app/cli.hpp"
#include "core/transcoder.hpp"
#include "util/logging.hpp"

int main(int argc, char** argv)
{
  try {
    auto cli = htj2k::app::parse_cli(argc, argv);
    if (cli.show_help) {
      std::cout << htj2k::app::usage_text();
      return 0;
    }
    cli.options.benchmark_mode = true;
    htj2k::util::set_log_level(cli.options.log_level);
    htj2k::core::Transcoder transcoder(htj2k::app::current_build_info(), cli.options);
    const auto report = transcoder.run();
    std::cout << report.summary_text() << '\n';
    return report.exit_code();
  } catch (const std::exception& ex) {
    std::cerr << "fatal: " << ex.what() << '\n';
    return 2;
  }
}
