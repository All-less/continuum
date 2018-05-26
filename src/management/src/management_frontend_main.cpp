
#include <cxxopts.hpp>

#include <continuum/config.hpp>
#include <continuum/constants.hpp>

#include "management_frontend.hpp"

int main(int argc, char* argv[]) {
  cxxopts::Options options("management_frontend",
                           "Continuum management interface");

  // clang-format off
  options.add_options()
    ("redis_ip", "Redis address", cxxopts::value<std::string>()->default_value("localhost"))
    ("redis_port", "Redis port", cxxopts::value<int>()->default_value("6379"));
  // clang-format on
  options.parse(argc, argv);

  continuum::Config& conf = continuum::get_config();
  conf.set_redis_address(options["redis_ip"].as<std::string>());
  conf.set_redis_port(options["redis_port"].as<int>());
  conf.ready();
  management::RequestHandler rh(continuum::MANAGEMENT_FRONTEND_PORT, 1);
  rh.start_listening();
}
