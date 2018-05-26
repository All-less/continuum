#include <string>
#include <utility>
#include <vector>

#include <continuum/datatypes.hpp>
#include <continuum/json_util.hpp>
#include <continuum/logging.hpp>
#include <continuum/retrain_policies.hpp>
#include <continuum/util.hpp>

namespace continuum {

std::string SpeculativeBestEffortPolicy::get_name() {
  return "SpeculativeBestEffortPolicy";
}

RetrainInfo SpeculativeBestEffortPolicy::ready_to_retrain(
    const RetrainRecord& record) const {
  /* first retrain*/
  if (record.data_from_ <= 0) {
    return RetrainInfo(true, record.last_arrival_,
                       record.last_arrival_);
  }

  if (record.finished_ &&
      record.last_arrival_ >
          record.training_batch_.first) {
    return RetrainInfo(true, record.data_to_ + 1,
                       record.last_arrival_);
  }

  // the B in algorithm analysis
  long data_size_after_retrain = 0;
  for (auto itr = record.batches_->begin();
       itr != record.batches_->end(); itr++) {
    if (itr->first > record.data_to_) {
      data_size_after_retrain += itr->second;
    }
  }

  // the D in algorithm analysis
  long last_retrain_data_size =
      record.training_batch_.second;

  // the delta in algorithm analysis  (milliseconds)
  long interval = (record.last_arrival_ -
                   record.training_batch_.first) /
                  1000;

  long left = data_size_after_retrain * record.beta_;
  long right =
      2 *
      (record.alpha_ * last_retrain_data_size * data_size_after_retrain +
       interval * (last_retrain_data_size + data_size_after_retrain));

  log_debug_formatted(
      LOGGING_TAG_RETRAIN,
      "In SpeculativeBestEffortPolicy.ready_to_retrain. B:{} D:{} "
      "delta:{} alpha:{} beta:{} left:{} right:{}",
      data_size_after_retrain, last_retrain_data_size, interval,
      record.alpha_, record.beta_, left, right);

  if (left >= right) {
    return RetrainInfo(true, record.data_from_,
                       record.last_arrival_);
  }

  return RetrainInfo(false, 0, 0);
}

RetrainInfo SpeculativeBestEffortPolicy::on_retrain_finished(
    const RetrainRecord& retrain_data) const {
  log_debug_formatted(
      LOGGING_TAG_RETRAIN,
      "In SpeculativeBestEffortPolicy.on_retrain_finished. last_arrival:{} "
      "last_trigger:{}",
      retrain_data.last_arrival_,
      retrain_data.training_batch_.first);

  if (retrain_data.finished_ &&
      retrain_data.last_arrival_ >
          retrain_data.training_batch_.first) {
    return RetrainInfo(true, retrain_data.data_to_ + 1,
                       retrain_data.last_arrival_);
  }
  return RetrainInfo(false, 0, 0);
}

long SpeculativeBestEffortPolicy::calc_timeout(
        __attribute__((unused)) const RetrainRecord &retrain_data) const {
  return 0;
}

std::string CostAwarePolicy::get_name() { return "CostAwarePolicy"; }

RetrainInfo CostAwarePolicy::ready_to_retrain(
    const RetrainRecord& record) const {
  if (!record.finished_) {
    return RetrainInfo(false, 0, 0);
  }

  // only one data sample
  if (record.batches_->size() <= 1) {
    return RetrainInfo(false, 0, 0);
  }

  auto cost_info = calc_cost_info(record);

  log_debug_formatted(
      LOGGING_TAG_RETRAIN,
      "In CostAwarePolicy.ready_to_retrain. all_cost:{} min_cost:{} "
      "gap_objective:{} weight:{} alpha:{} beta:{}",
      cost_info.first, cost_info.second, record.weight_ * record.beta_,
      record.weight_, record.alpha_, record.beta_);

  if (cost_info.first - cost_info.second > record.weight_ * record.beta_) {
    return RetrainInfo(true,
                       record.data_to_ + 1,
                       record.last_arrival_);
  } else {
    return RetrainInfo(false, 0, 0);
  }
}

RetrainInfo CostAwarePolicy::on_retrain_finished(
        const RetrainRecord& record) const {
  return ready_to_retrain(record);
}

long CostAwarePolicy::calc_timeout(const RetrainRecord &record) const {
  auto costs = calc_cost_info(record);
  int untrained = 0;
  for (auto &b : *record.batches_) {
    if (// either all data is untrained
        record.finished_ ||
        // or it doesn't belong to training data
        (!record.finished_ && b.first > record.data_to_)) {
      untrained += b.second;
    }
  }
  return (record.weight_ * record.beta_ -
         (costs.first - costs.second)) / untrained;
}

double CostAwarePolicy::calc_cost(
        BatchesInfo::iterator begin, BatchesInfo::iterator end,
        long data_size, double alpha, double beta, double weight) const {

  long retrain_time =
      static_cast<long>(alpha * data_size + beta); /* millisecond */

  long end_time = retrain_time * 1000 + (end - 1)->first; /* microsecond */
  long latency = 0;
  for (auto itr = begin; itr != end; itr++) {
    latency += (end_time - itr->first);
  }

  return weight * retrain_time + latency / 1000;
}

CostInfo CostAwarePolicy::calc_cost_info(
        const RetrainRecord &record) const {

  auto begin = record.batches_->begin();
  // exclude all training data
  if (!record.finished_) {
    for (; begin != record.batches_->end(); begin++) {
      if (begin->first > record.data_to_) {
        break;
      }
    }
  }

  // count number of data samples
  long data_size = 0;
  for (auto i = begin; i != record.batches_->end(); i++) {
    data_size += i->second;
  }


  auto itr = begin;
  long partial_size = 0; // number of data samples from `begin` to `iter`
  for (; itr != record.batches_->end(); itr++) {
    if (data_size - (itr->second) > 2 * partial_size) {
      partial_size += itr->second;
    } else {
      break;
    }
  }

  double all_cost = calc_cost(begin, record.batches_->end(),
                              data_size, record.alpha_,
                              record.beta_, record.weight_);

  double min_cost = calc_cost(begin, itr,
                              partial_size, record.alpha_,
                              record.beta_, record.weight_) +
                    calc_cost(itr, record.batches_->end(),
                              data_size - partial_size, record.alpha_,
                              record.beta_, record.weight_);

  return {all_cost, min_cost};
}

std::string NaiveBestEffortPolicy::get_name() {
  return "NaiveBestEffortPolicy";
}

RetrainInfo NaiveBestEffortPolicy::ready_to_retrain(
    const RetrainRecord& record) const {
  /* first retrain*/
  if (record.data_from_ <= 0) {
    return RetrainInfo(true, 1, record.last_arrival_);
  }

  if (record.finished_ &&
      record.last_arrival_ > record.training_batch_.first) {
    return RetrainInfo(true,
                       record.data_to_ + 1,
                       record.last_arrival_);
  }

  return RetrainInfo(false, 0, 0);
}

RetrainInfo NaiveBestEffortPolicy::on_retrain_finished(
    const RetrainRecord& record) const {
  if (record.finished_ &&
      record.last_arrival_ > record.training_batch_.first) {
    return RetrainInfo(true,
                       record.data_to_ + 1,
                       record.last_arrival_);
  }
  return RetrainInfo(false, 0, 0);
}

long NaiveBestEffortPolicy::calc_timeout(
        __attribute__((unused)) const RetrainRecord &retrain_data) const {
  return 0;
}

std::string ManualPolicy::get_name() { return "ManualPolicy"; }

RetrainInfo ManualPolicy::ready_to_retrain(
        __attribute__((unused)) const RetrainRecord& retrain_data) const {
  return RetrainInfo(false, 0, 0);
}

RetrainInfo ManualPolicy::on_retrain_finished(
        __attribute__((unused)) const RetrainRecord& retrain_data) const {
  return RetrainInfo(false, 0, 0);
}

long ManualPolicy::calc_timeout(
        __attribute__((unused)) const RetrainRecord &retrain_data) const {
  return 0;
}

}  // namespace continuum
