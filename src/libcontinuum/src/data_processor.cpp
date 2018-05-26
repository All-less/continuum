#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

#include <folly/futures/Future.h>

#include <boost/exception_ptr.hpp>

#include <continuum/data_processor.hpp>
#include <continuum/datatypes.hpp>
#include <continuum/exceptions.hpp>
#include <continuum/logging.hpp>
#include <continuum/retrain_policies.hpp>

#include <continuum/config.hpp>
#include <continuum/constants.hpp>

using std::tuple;
using std::vector;

namespace continuum {

DataProcessor::DataProcessor()
    : rpc_(std::make_shared<rpc::RPCBackendService>()),
      checker_(std::make_shared<TriggerChecker>(rpc_)) {
  log_info(LOGGING_TAG_DATA_PROCESSOR, "Data processor started.");
  rpc_->start("*", RPC_BACKEND_SERVICE_PORT,
              [this](rpc::RPCBackendResponse response) {
                // TODO add error handling
                on_retrain_started(response);
              },
              [this](rpc::RPCBackendResponse response) {
                // TODO add error handling
                on_retrain_finished(response);
              });
}

folly::Future<bool> DataProcessor::update_retrain_trigger_data(
    std::string app_name, long arrival_time, long data_amount) {
  folly::Promise<bool> pro_report_data_arrival;
  auto future = pro_report_data_arrival.getFuture();
  checker_->report_data_arrival(app_name, arrival_time, data_amount,
                                std::move(pro_report_data_arrival));
  return future;
}

folly::Future<long> DataProcessor::manual_retrain(std::string app_name) {
  folly::Promise<long> pro_manual_retrain;
  auto future = pro_manual_retrain.getFuture();
  checker_->manual_trigger_retrain(app_name, std::move(pro_manual_retrain));
  return future;
}

void DataProcessor::on_retrain_started(rpc::RPCBackendResponse response) {
  checker_->report_retrain_begin(response.first, folly::Promise<bool>());
}

void DataProcessor::on_retrain_finished(rpc::RPCBackendResponse response) {
  checker_->report_retrain_end(response.first, folly::Promise<bool>());
}

}  // namespace continuum
