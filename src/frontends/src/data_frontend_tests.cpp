#include <gtest/gtest.h>

#include <folly/futures/Future.h>
#include <continuum/data_processor.hpp>

#include "data_frontend.hpp"

using namespace continuum;
using namespace continuum::redis;
using namespace data_frontend;

namespace {

class MockDataProcessor {
 public:
  MockDataProcessor() = default;

  folly::Future<bool> update_retrain_trigger_data(std::string app_name,
                                                  const long arrival_time,
                                                  const long data_amount) {
    return folly::makeFuture(true);
  }

  folly::Future<long> manual_retrain(std::string app_name) {
    return folly::makeFuture(0);
  }
};

class DataFrontendTest : public ::testing::Test {
 public:
  RequestHandler<MockDataProcessor> rh_;
  std::shared_ptr<redox::Redox> redis_;
  std::shared_ptr<redox::Subscriber> subscriber_;

  DataFrontendTest()
      : rh_("0.0.0.0", 1337, 8),
        redis_(std::make_shared<redox::Redox>()),
        subscriber_(std::make_shared<redox::Subscriber>()) {
    Config &conf = get_config();
    redis_->connect(conf.get_redis_address(), conf.get_redis_port());
    subscriber_->connect(conf.get_redis_address(), conf.get_redis_port());

    // delete all keys
    send_cmd_no_reply<std::string>(*redis_, {"FLUSHALL"});

    send_cmd_no_reply<std::string>(
        *redis_, {"CONFIG", "SET", "notify-keyspace-events", "AKE"});
  }

  virtual ~DataFrontendTest() {
    subscriber_->disconnect();
    redis_->disconnect();
  }
};

// TEST_F(DataFrontendTest, TestTestFramework) {
//   EXPECT_EQ(0, 0);
// }

TEST_F(DataFrontendTest, TestUploadRetrainData) {
  std::string name = "my_app_name";
  InputType input_type = InputType::Doubles;
  std::string policy = "DefaultOutputSelectionPolicy";
  std::string default_output = "1.0";
  int latency_slo_micros = 10000;
  ASSERT_TRUE(add_application(*redis_, name, input_type, policy, default_output,
                              latency_slo_micros));
  std::string test_json_doubles =
      "{\"data\": [[1.1, 2.2], [10.1, 20.2], [100.1, 200.2]]}";
  std::vector<std::vector<double>> expected_input{
      {1.1, 2.2}, {10.1, 20.2}, {100.1, 200.2}};
  ASSERT_TRUE(rh_.decode_and_handle_upload(test_json_doubles, name).get());
}

}  // namespace
