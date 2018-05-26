#include <gtest/gtest.h>
#include <cxxopts.hpp>

#include <continuum/config.hpp>

int main(int argc, char** argv) {
  cxxopts::Options options("libcontinuum_tests", "LibContinuum tests");
  options.add_options()("p,redis_port", "Redis port",
                        cxxopts::value<int>()->default_value("-1"));
  options.parse(argc, argv);
  int redis_port = options["redis_port"].as<int>();
  // means the option wasn't supplied and an exception should be thrown
  if (redis_port == -1) {
    std::cerr << options.help() << std::endl;
    return -1;
  }

  continuum::Config& conf = continuum::get_config();
  conf.set_redis_port(options["redis_port"].as<int>());
  conf.ready();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
