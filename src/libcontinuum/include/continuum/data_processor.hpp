#ifndef CONTINUUM_DATA_PROCESSOR_HPP
#define CONTINUUM_DATA_PROCESSOR_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>

#include "config.hpp"
#include "constants.hpp"
#include "datatypes.hpp"
#include "metrics.hpp"
#include "retrain_policies.hpp"
#include "rpc_backend_service.hpp"
#include "timers.hpp"

namespace continuum {

const std::string LOGGING_TAG_DATA_PROCESSOR = "DATAPROCESSOR";

const std::string DEFAULT_POLICY = NaiveBestEffortPolicy::get_name();

class InflightRetrainMessage {
 public:
  InflightRetrainMessage(
      const long send_time, const int zmq_connection_id, const std::string app_name,
      const std::shared_ptr<std::vector<std::string>> retrain_data_ids)
      : send_time_(send_time),
        zmq_connection_id_(zmq_connection_id),
        app_name_(app_name),
        state_(RetrainState::RetrainSent),
        batch_ids_(retrain_data_ids),
        msg_link_(-1) {}

  InflightRetrainMessage() {}

  // Default copy and move constructors
  InflightRetrainMessage(const InflightRetrainMessage&) = default;
  InflightRetrainMessage(InflightRetrainMessage&&) = default;

  // Default assignment operators
  InflightRetrainMessage& operator=(const InflightRetrainMessage&) = default;
  InflightRetrainMessage& operator=(InflightRetrainMessage&&) = default;

  long send_time_;
  int zmq_connection_id_;
  std::string app_name_;
  RetrainState state_;
  std::shared_ptr<std::vector<std::string>> batch_ids_;
  int msg_link_;
};



class RuntimeProfiler {
 public:
  // (training_time, data_size)
  using Samples = std::vector<std::pair<long, long>>;

  // the maximal number of samples to keep
  const int DEFAULT_MAX_SAMPLES = 10;
  // only do regression when sample num exceeds threshold
  const int DEFAULT_THRESHOLD = 3;

  ~RuntimeProfiler() = default;

  RuntimeProfiler()
      : max_samples_(DEFAULT_MAX_SAMPLES),
        threshold_(DEFAULT_THRESHOLD),
        alpha_(DEFAULT_ALPHA),
        beta_(DEFAULT_BETA),
        time_size_pairs_(std::make_shared<Samples>()) {}

  RuntimeProfiler(int max_samples, int threshold)
      : max_samples_(max_samples),
        threshold_(threshold),
        alpha_(DEFAULT_ALPHA),
        beta_(DEFAULT_BETA),
        time_size_pairs_(std::make_shared<Samples>()) {}

  RuntimeProfiler(double alpha, double beta)
      : max_samples_(DEFAULT_MAX_SAMPLES),
        threshold_(DEFAULT_THRESHOLD),
        alpha_(alpha),
        beta_(beta),
        time_size_pairs_(std::make_shared<Samples>()) {}

  void add_sample(long time, long data_size) {
    if (time_size_pairs_->size() >= (unsigned)max_samples_) {
      time_size_pairs_->erase(time_size_pairs_->begin());
    }
    time_size_pairs_->push_back({time, data_size});

    if (time_size_pairs_->size() >= (unsigned)threshold_) {
        calc_alpha_beta();
    }
  }

  void calc_alpha_beta() {
    int size = time_size_pairs_->size();

    long sum_time = 0.0;
    long sum_size = 0.0;

    long sum_x_multi_y = 0.0;
    long multi_sum_x_sum_y = 0.0;
    long sum_x_squa = 0.0;
    long squa_sum_x = 0.0;

    for (int i = 0; i < size; i++) {
      long y = time_size_pairs_->at(i).first;
      long x = time_size_pairs_->at(i).second;

      sum_time += y;
      sum_size += x;
      sum_x_multi_y += x * y;
      sum_x_squa += x * x;
    }

    multi_sum_x_sum_y = sum_time * sum_size;
    squa_sum_x = sum_size * sum_size;

    alpha_ = (size * sum_x_multi_y - multi_sum_x_sum_y + 0.0) /
             (size * sum_x_squa - squa_sum_x);
    beta_ = (sum_time + 0.0) / size - (alpha_ * sum_size + 0.0) / size;
  }

  std::pair<double, double> get_alpha_beta() {
    return {alpha_, beta_};
  }

  int max_samples_;
  int threshold_;
  double alpha_;
  double beta_;

  // (training_time, data_size)
  std::shared_ptr<Samples> time_size_pairs_;
};

class TriggerChecker {
 public:
  ~TriggerChecker() {
    redis_connection_.disconnect();
    redis_subscriber_.disconnect();
  }

  TriggerChecker(std::shared_ptr<rpc::RPCBackendService> rpc) : rpc_(rpc) {
    retrain_policies_.emplace(NaiveBestEffortPolicy::get_name(),
                              std::make_shared<NaiveBestEffortPolicy>());
    retrain_policies_.emplace(SpeculativeBestEffortPolicy::get_name(),
                              std::make_shared<SpeculativeBestEffortPolicy>());
    retrain_policies_.emplace(CostAwarePolicy::get_name(),
                              std::make_shared<CostAwarePolicy>());
    retrain_policies_.emplace(ManualPolicy::get_name(),
                              std::make_shared<ManualPolicy>());

    continuum::Config& conf = continuum::get_config();
    while (!redis_connection_.connect(conf.get_redis_address(),
                                      conf.get_redis_port())) {
      continuum::log_error(LOGGING_TAG_DATA_PROCESSOR,
                         "Data processor failed to connect to Redis.",
                         "Retrying in 1 second...");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!redis_subscriber_.connect(conf.get_redis_address(),
                                      conf.get_redis_port())) {
      continuum::log_error(
          LOGGING_TAG_DATA_PROCESSOR,
          "Data processor subscriber failed to connect to Redis.",
          "Retrying in 1 second...");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    base_ = std::make_unique<folly::EventBase>();
    event_base_thread_ = std::make_unique<std::thread>(
        [this]() {
          continuum::redis::subscribe_to_backend_link_changes(
              redis_subscriber_,
              [this](const std::string& key, const std::string& event_type) {
                if (event_type == "set") {
                  auto backend_name = continuum::redis::get_backend_link(
                          redis_connection_, key);
                  auto backend = continuum::redis::get_backend(
                          redis_connection_, backend_name);
                  zmq_connections[key] = std::stoi(backend["zmq_connection_id"]);
                }
              });
          base_->loopForever();
        });
  }

  void manual_trigger_retrain(std::string app_name, folly::Promise<long> p) {
    base_->runInEventBaseThread([ this, app_name, p = std::move(p) ]() mutable {

      if (retrain_records_.find(app_name) != retrain_records_.end()) {
        // count how many new data have arrived since last retrain
        auto record = retrain_records_[app_name];
        long last_end = record->data_to_;  // the end of last retrain
        long data_size = 0;
        for (auto &p: *record->batches_) {
          if (p.first >= last_end) {
            data_size += p.second;
          }
        }

        if (data_size > 0) {  // there are some new data
          long cur_time = get_cur_time();
          std::string backend_name = redis::get_backend_link(redis_connection_,
                                                             app_name);
          auto data_ids = redis::get_retrain_data_ids(redis_connection_,
                                                      app_name,
                                                      last_end,
                                                      cur_time);
          RetrainRequest retrain_query(last_end, cur_time,
                                       data_ids, data_size,
                                       RetrainType::StartRetrain);
          trigger_retrain(app_name, retrain_query);

          p.setValue(data_size);
        } else {
          p.setValue(0);
        }
      } else {
        continuum::log_error_formatted(LOGGING_TAG_DATA_PROCESSOR,
                                     "No historical data found upon manual "
                                     "trigger for app: {}.",
                                     app_name);
        p.setValue(-1);
      }

    });
  }

  void report_data_arrival(std::string app_name, long arrival_time,
                           long data_size, folly::Promise<bool> p) {
    base_->runInEventBaseThread([
      this, app_name, arrival_time, data_size, p = std::move(p)
    ]() mutable {

      if (retrain_records_.find(app_name) != retrain_records_.end()) {
        // all data structures related to the app has been initialized,
        // so we just need to record data arrival
        auto record = retrain_records_[app_name];
        record->batches_->push_back({arrival_time, data_size});
        record->last_arrival_ = arrival_time;
      } else {
        // no previous metadata found for the app, which means we need to
        // initialize related data structure for it
        std::string backend_name =
            redis::get_backend_link(redis_connection_, app_name);
        if ("" == backend_name) {
          continuum::log_error_formatted(
              LOGGING_TAG_DATA_PROCESSOR,
              "No backend found when receiving data from app: {}",
              app_name);
          p.setValue(true);
          return;
        }

        auto backend = redis::get_backend(redis_connection_, backend_name);
        double alpha = std::stod(backend["alpha"]);
        double beta = std::stod(backend["beta"]);
        double weight = std::stod(backend["weight"]);

        // create a new retrain record for the app.
        auto record = std::make_shared<RetrainRecord>(arrival_time);
        record->batches_->push_back({arrival_time, data_size});
        record->weight_ = weight;
        record->alpha_ = alpha;
        record->beta_ = beta;
        retrain_records_.emplace(app_name, record);

        // setup the runtime profiler
        auto profiler = std::make_shared<RuntimeProfiler>(alpha, beta);
        runtime_profilers_.emplace(app_name, profiler);

        // set retrain policy for the app
        std::string policy = backend["policy"];
        set_app_policy(app_name, policy);
      }
      p.setValue(true);
      check_trigger(app_name, arrival_time);
    });
  }

  void report_retrain_begin(int msg_id, folly::Promise<bool> p) {
    base_->runInEventBaseThread([ this, msg_id, p = std::move(p) ]() mutable {
      if (inflight_messages_.find(msg_id) != inflight_messages_.end()) {
        inflight_messages_[msg_id]->state_ = RetrainState::StartedReceived;
      }
      p.setValue(true);
    });
  }

  void report_retrain_end(int msg_id, folly::Promise<bool> p) {
    base_->runInEventBaseThread([ this, msg_id, p = std::move(p) ]() mutable {
      if (inflight_messages_.find(msg_id) != inflight_messages_.end()) {
        long cur_time = get_cur_time();
        auto msg = inflight_messages_[msg_id];
        std::string app_name = msg->app_name_;

        // erase corresponding inflight message with all descendants
        int next_msg = msg->msg_link_;
        inflight_messages_.erase(msg_id);
        while (next_msg >= 0) {
          int tmp = next_msg;
          next_msg = inflight_messages_[next_msg]->msg_link_;
          inflight_messages_.erase(tmp);
        }

        // update retrain records
        if (retrain_records_.find(app_name) != retrain_records_.end()) {
          auto record = retrain_records_[app_name];
          record->finished_ = true;

          // remove batches that have been trained
          long erase_from = record->data_from_;
          long erase_to = record->data_to_;
          auto iter = record->batches_->begin();
          while (iter != record->batches_->end()) {
            if (iter->first >= erase_from && iter->first <= erase_to) {
              iter = record->batches_->erase(iter);
            } else {
              iter++;
            }
          }

          // update runtime profiler
          long time = (cur_time - record->training_batch_.first) / 1000;
          long size = record->training_batch_.second;
          runtime_profilers_[app_name]->add_sample(time, size);
          record->alpha_ = runtime_profilers_[app_name]->alpha_;
          record->beta_ = runtime_profilers_[app_name]->beta_;

          continuum::log_info_formatted(
              LOGGING_TAG_DATA_PROCESSOR,
              "Retrain ended. app:{} trigger_time:{} cur_time:{} "
              "retrain_time:{} alpha:{} beta:{}",
              app_name, record->training_batch_.first, cur_time, time,
              runtime_profilers_[app_name]->alpha_,
              runtime_profilers_[app_name]->beta_);
        }
        p.setValue(true);

        // check whether we need to trigger next retrain
        auto policy = get_app_policy(app_name);
        check_trigger_by_func(
                app_name, std::bind(&RetrainPolicy::on_retrain_finished, policy,
                                    std::placeholders::_1));
      }

    });
  }

 private:

  long get_cur_time() {
    auto duration = std::chrono::system_clock::now().time_since_epoch();
    auto mu_sec = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return mu_sec.count();
  }

  void set_app_policy(std::string app_name, std::string policy_name) {
    if (retrain_policies_.find(policy_name) != retrain_policies_.end()) {
      app_policies_[app_name] = policy_name;
    } else {
      app_policies_[app_name] = DEFAULT_POLICY;
    }
  }

  std::shared_ptr<RetrainPolicy> &get_app_policy(std::string app_name) {
    if (app_policies_.find(app_name) != app_policies_.end()) {
      return retrain_policies_[app_policies_[app_name]];
    } else {
      throw std::runtime_error("No policy found for app:" + app_name);
    }
  }

  void trigger_retrain(std::string app_name, RetrainRequest &retrain_req) {

    // this function will only be called when backend
    // has been linked to the app
    int zmq_connection_id = zmq_connections[app_name];
    int msg_id = rpc_->send_message(retrain_req.serialize(),
                                    zmq_connection_id);

    // update retrain record
    long cur_time = get_cur_time();
    auto record = retrain_records_[app_name];
    record->data_from_ = retrain_req.data_from_;
    record->data_to_ = retrain_req.data_to_;
    record->training_batch_.first = cur_time;
    record->training_batch_.second = retrain_req.data_size_;
    record->finished_ = false;

    // continuum::log_debug_formatted(
    //     LOGGING_TAG_DATA_PROCESSOR,
    //     "In trigger_retrain, after setting record.\n{}",
    //     record->debug_string());

    // create a new inflight message
    auto inflight_message = std::make_shared<InflightRetrainMessage>(
        cur_time, zmq_connection_id, app_name,
        std::make_shared<std::vector<std::string>>(
            retrain_req.batch_ids_));

    // find msg_id of last inflight message for the same app
    int link = -1;
    long last_time = 0;
    for (auto &p : inflight_messages_) {
      if (p.second->app_name_ == app_name) {
        auto time = p.second->send_time_;
        if (time > last_time) {
          last_time = time;
          link = p.first;
        }
      }
    }

    inflight_message->msg_link_ = link;
    inflight_messages_.emplace(msg_id, inflight_message);


    log_info_formatted(LOGGING_TAG_DATA_PROCESSOR,
                       "Trigger retrain. batch_num:{} data_size:{} cur_time:{}",
                       retrain_req.batch_ids_.size(),
                       retrain_req.data_size_, cur_time);

    log_debug_formatted(LOGGING_TAG_DATA_PROCESSOR,
                        "trigger_time:{} msg_id:{} data_from:{} data_to:{} ",
                        record->training_batch_.first, msg_id,
                        record->data_from_,
                        record->data_to_);
  }

  void set_timeout(std::string app_name, long last_arrival,
                   long timeout, folly::Promise<RetrainInfo> p) {
    if (timeout > 0) {
      base_->tryRunAfterDelay(
          [this, app_name, last_arrival, timeout, p = std::move(p) ]() mutable {
            if (retrain_records_.find(app_name) != retrain_records_.end()) {
              auto record = retrain_records_[app_name];
              if (record->last_arrival_ == last_arrival &&
                  record->finished_) {
                // no new data has arrived
                p.setValue(RetrainInfo(true,
                                       record->data_to_ + 1,
                                       last_arrival));
                return;
              }
            }
            p.setValue(RetrainInfo(false, 0, 0));
          },
          timeout /* milliseconds */);
    } else {
      p.setValue(RetrainInfo(false, 0, 0));
    }
  }

  void check_trigger(std::string app_name, long arrival_time) {

    // Check whether to trigger retrain. If yes, it will
    // directly trigger retrain.
    auto policy = get_app_policy(app_name);
    auto triggered = check_trigger_by_func(
            app_name, std::bind(&RetrainPolicy::ready_to_retrain, policy,
                                std::placeholders::_1));

    // if it hasn't triggered retrain
    if (!triggered) {
      auto record = retrain_records_[app_name];
      // continuum::log_debug_formatted(
      //     LOGGING_TAG_DATA_PROCESSOR,
      //     "In check_trigger, before calc_timeout.\n{}",
      //     record->debug_string());
      long timeout = policy->calc_timeout(*record);

      folly::Promise<RetrainInfo> promise;
      auto future = promise.getFuture();

      future.then([this, app_name, record](RetrainInfo retrain_info) {

        if (std::get<0>(retrain_info)) {
          continuum::log_debug_formatted(
              LOGGING_TAG_DATA_PROCESSOR,
              "Trigger retrain after timeout. app:{}",
              app_name);

          long data_from = std::get<1>(retrain_info);
          long data_to = std::get<2>(retrain_info);
          auto data_ids = redis::get_retrain_data_ids(
              redis_connection_, app_name, data_from, data_to);

          long data_size = 0;
          for (auto &p : *record->batches_) {
            if (p.first >= data_from && p.first <= data_to) {
              data_size += p.second;
            }
          }


          RetrainRequest retrain_query(data_from, data_to,
                                     data_ids, data_size,
                                     RetrainType::StartRetrain);

          trigger_retrain(app_name, retrain_query);
        }
      });

      continuum::log_debug_formatted(
          LOGGING_TAG_DATA_PROCESSOR,
          "Set timeout. app:{}, last_arrival:{}, timeout:{}",
          app_name, arrival_time, timeout);

      // this must be called after future.then, or it will incur segfault
      set_timeout(app_name, arrival_time, timeout,
                  std::move(promise));
    }
  }

  bool check_trigger_by_func(
          std::string app_name,
          const std::function<RetrainInfo(RetrainRecord)> &func) {

    auto record = retrain_records_[app_name];
    auto retrain_info = func(*record);

    continuum::log_debug_formatted(
        LOGGING_TAG_DATA_PROCESSOR,
        "In check_trigger_by_func. app:{} decision:{}",
        app_name, std::get<0>(retrain_info));

    if (std::get<0>(retrain_info)) {
      long data_from = std::get<1>(retrain_info);
      long data_to = std::get<2>(retrain_info);
      auto batch_ids = redis::get_retrain_data_ids(
          redis_connection_, app_name, data_from, data_to);

      long data_size = 0;
      for (auto &p : *record->batches_) {
        if (p.first >= data_from && p.first <= data_to) {
          data_size += p.second;
        }
      }

      RetrainRequest retrain_query(data_from, data_to, batch_ids,
                                 data_size, RetrainType::StartRetrain);
      trigger_retrain(app_name, retrain_query);

      continuum::log_debug_formatted(
          LOGGING_TAG_DATA_PROCESSOR,
          "Triggered by check_trigger_by_func. app:{} "
          "data_from:{} data_to:{}",
          app_name, data_from, data_to);


      return true;
    } else {
      return false;
    }
  }

  std::shared_ptr<rpc::RPCBackendService> rpc_;

  std::unique_ptr<folly::EventBase> base_;
  std::unique_ptr<std::thread> event_base_thread_;
  std::unordered_map<std::string, std::shared_ptr<RetrainRecord>>
      retrain_records_;

  redox::Redox redis_connection_;
  redox::Subscriber redis_subscriber_;

  std::unordered_map<int, std::shared_ptr<InflightRetrainMessage>>
      inflight_messages_;

  // policy_name -> policy
  std::unordered_map<std::string, std::shared_ptr<RetrainPolicy>>
      retrain_policies_;
  // app_name -> policy_name
  std::unordered_map<std::string, std::string> app_policies_;

  std::unordered_map<std::string, std::shared_ptr<RuntimeProfiler>>
      runtime_profilers_;

  // app_name -> zmq_connection_id
  std::unordered_map<std::string, int> zmq_connections;
};

class DataProcessor {
 public:
  ~DataProcessor() = default;

  DataProcessor();

  // no copies
  DataProcessor(const DataProcessor& other) = delete;
  DataProcessor& operator=(const DataProcessor& other) = delete;

  // move constructor and assignment
  DataProcessor(DataProcessor&& other) = default;
  DataProcessor& operator=(DataProcessor&& other) = default;

  void on_retrain_started(rpc::RPCBackendResponse response);
  void on_retrain_finished(rpc::RPCBackendResponse response);

  folly::Future<bool> update_retrain_trigger_data(std::string app_name,
                                                  long arrival_time,
                                                  long data_amount);

  folly::Future<long> manual_retrain(std::string app_name);

 private:
  std::shared_ptr<rpc::RPCBackendService> rpc_;
  std::shared_ptr<TriggerChecker> checker_;
};

}  // namespace continuum

#endif  // CONTINUUM_DATA_PROCESSOR_HPP
