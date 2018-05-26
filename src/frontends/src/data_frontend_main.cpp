#include <cxxopts.hpp>
#include <spdlog/spdlog.h>

#include <continuum/config.hpp>
#include <continuum/constants.hpp>
#include <continuum/data_processor.hpp>
#include <continuum/logging.hpp>

#include "data_frontend.hpp"

int main(int argc, char* argv[]) {
  cxxopts::Options options("data_frontend", "Continuum data uploading frontend");
  // clang-format off
  options.add_options()
    ("redis_ip", "Redis address",
        cxxopts::value<std::string>()->default_value(continuum::DEFAULT_REDIS_ADDRESS))
    ("redis_port", "Redis port",
        cxxopts::value<int>()->default_value(std::to_string(continuum::DEFAULT_REDIS_PORT)));
  // clang-format on
  options.parse(argc, argv);

  spdlog::set_level(spdlog::level::info);

  continuum::Config& conf = continuum::get_config();
  conf.set_redis_address(options["redis_ip"].as<std::string>());
  conf.set_redis_port(options["redis_port"].as<int>());
  conf.ready();

  data_frontend::RequestHandler<continuum::DataProcessor> rh(
      "0.0.0.0", continuum::DATA_FRONTEND_PORT, 1);
  rh.start_listening();
  return 0;
}
