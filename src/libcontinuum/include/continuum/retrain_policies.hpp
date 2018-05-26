#ifndef CONTINUUM_RETRAIN_POLICY_HPP
#define CONTINUUM_RETRAIN_POLICY_HPP

#include <vector>

#include <string>
#include "timers.hpp"

namespace continuum {

const std::string LOGGING_TAG_RETRAIN = "RETRAIN_POLICY";

// (whether to) trigger_retrain, start_time (of data), end_time
using RetrainInfo = std::tuple<bool, long, long>;
// arrival_time, batch_size
using BatchInfo = std::pair<long, long>;
using BatchesInfo = std::vector<BatchInfo>;

class RetrainRecord {
 public:
  RetrainRecord() = default;

  RetrainRecord(long latest_data_arrival_time)
      : last_arrival_(latest_data_arrival_time),
        data_from_(0),
        data_to_(0),
        finished_(true),
        batches_(std::make_shared<BatchesInfo>()){};

  // Default copy and move constructors
  RetrainRecord(const RetrainRecord&) = default;
  RetrainRecord(RetrainRecord&&) = default;

  // Default assignment operators
  RetrainRecord& operator=(const RetrainRecord&) = default;
  RetrainRecord& operator=(RetrainRecord&&) = default;

  std::string debug_string() {
    std::stringstream ss;
    ss << "last_arrival_:" << last_arrival_ << " data_from_:" << data_from_
       << " data_to_:" << data_to_ << " finished_:" << finished_
       << "\nalpha_:" << alpha_ << " beta_:" << beta_ << " weight_:" << weight_
       << " training_batch_:" << std::get<0>(training_batch_) << "," << std::get<1>(training_batch_)
       << "\nbatches_:[ ";
    for (auto &b : *batches_) {
        ss << std::get<0>(b) << "," << std::get<1>(b) << " ";
    }
    ss << "]";
    return ss.str();
  }

  double alpha_;
  double beta_;
  double weight_;

  long last_arrival_;
  long data_from_; // start time of data
  long data_to_; // end time of data
  bool finished_;
  BatchInfo training_batch_; // information about currently-training data
  std::shared_ptr<BatchesInfo> batches_;
};

class RetrainPolicy {
 public:
  RetrainPolicy() = default;
  RetrainPolicy(const RetrainPolicy&) = default;
  RetrainPolicy& operator=(const RetrainPolicy&) = default;
  RetrainPolicy(RetrainPolicy&&) = default;
  RetrainPolicy& operator=(RetrainPolicy&&) = default;
  virtual ~RetrainPolicy() = default;

  virtual RetrainInfo ready_to_retrain(
      const RetrainRecord& retrain_data) const = 0;
  virtual RetrainInfo on_retrain_finished(
      const RetrainRecord& retrain_data) const = 0;
  virtual long calc_timeout(const RetrainRecord &retrain_data) const = 0;
};

/// Derived Retrain Policies
class SpeculativeBestEffortPolicy : public RetrainPolicy {
 public:
  SpeculativeBestEffortPolicy() = default;
  SpeculativeBestEffortPolicy(const SpeculativeBestEffortPolicy&) = default;
  SpeculativeBestEffortPolicy& operator=(const SpeculativeBestEffortPolicy&) =
      default;
  SpeculativeBestEffortPolicy(SpeculativeBestEffortPolicy&&) = default;
  SpeculativeBestEffortPolicy& operator=(SpeculativeBestEffortPolicy&&) =
      default;
  ~SpeculativeBestEffortPolicy() = default;

  static std::string get_name();

  RetrainInfo ready_to_retrain(
      const RetrainRecord& record) const override;
  RetrainInfo on_retrain_finished(
      const RetrainRecord& retrain_data) const override;
  long calc_timeout(const RetrainRecord &retrain_data) const override;
};

/**
 * CostInfo: (all_cost, min_cost)
 *
 * all_cost: the cost to retrain all data up until now
 * min_cost: minimal cost if we add another imaginary retrain
 */
using CostInfo = std::pair<long, long>;

class CostAwarePolicy : public RetrainPolicy {
 public:
  CostAwarePolicy() = default;
  CostAwarePolicy(const CostAwarePolicy&) = default;
  CostAwarePolicy& operator=(const CostAwarePolicy&) = default;
  CostAwarePolicy(CostAwarePolicy&&) = default;
  CostAwarePolicy& operator=(CostAwarePolicy&&) = default;
  ~CostAwarePolicy() = default;

  static std::string get_name();

  RetrainInfo ready_to_retrain(
      const RetrainRecord& record) const override;
  RetrainInfo on_retrain_finished(
      const RetrainRecord& retrain_data) const override;
  long calc_timeout(const RetrainRecord &record) const override;

  double calc_cost(BatchesInfo::iterator begin,
                   BatchesInfo::iterator end,
                   long data_size, double alpha,
                   double beta, double weight) const;

  CostInfo calc_cost_info(const RetrainRecord &record) const;
};

class NaiveBestEffortPolicy : public RetrainPolicy {
 public:
  NaiveBestEffortPolicy() = default;
  NaiveBestEffortPolicy(const NaiveBestEffortPolicy&) = default;
  NaiveBestEffortPolicy& operator=(const NaiveBestEffortPolicy&) = default;
  NaiveBestEffortPolicy(NaiveBestEffortPolicy&&) = default;
  NaiveBestEffortPolicy& operator=(NaiveBestEffortPolicy&&) = default;
  ~NaiveBestEffortPolicy() = default;

  static std::string get_name();

  RetrainInfo ready_to_retrain(
      const RetrainRecord& record) const override;
  RetrainInfo on_retrain_finished(
      const RetrainRecord& record) const override;
  long calc_timeout(const RetrainRecord &retrain_data) const override;
};

class ManualPolicy : public RetrainPolicy {
 public:
  ManualPolicy() = default;
  ManualPolicy(const ManualPolicy&) = default;
  ManualPolicy& operator=(const ManualPolicy&) = default;
  ManualPolicy(ManualPolicy&&) = default;
  ManualPolicy& operator=(ManualPolicy&&) = default;
  ~ManualPolicy() = default;

  static std::string get_name();

  RetrainInfo ready_to_retrain(
      const RetrainRecord& retrain_data) const override;
  RetrainInfo on_retrain_finished(
      const RetrainRecord& retrain_data) const override;
  long calc_timeout(const RetrainRecord &retrain_data) const override;
};

}  // namespace continuum

#endif  // CONTINUUM_RETRAIN_POLICY_HPP
