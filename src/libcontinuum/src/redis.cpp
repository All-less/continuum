#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <continuum/constants.hpp>
#include <continuum/logging.hpp>
#include <continuum/redis.hpp>
#include <redox.hpp>

using redox::Command;
using redox::Redox;
using redox::Subscriber;
using std::string;
using std::unordered_map;
using std::vector;

namespace continuum {
namespace redis {

const std::string VERSION_METADATA_PREFIX = "CURRENT_MODEL_VERSION:";

bool contains_prohibited_chars_for_group(std::string value) {
  for (std::string prohibited_str : continuum::redis::prohibited_group_strings) {
    if (value.find(prohibited_str) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::unordered_map<string, string> parse_redis_map(
    const std::vector<string>& redis_data) {
  std::unordered_map<string, string> parsed_map;
  for (auto m = redis_data.begin(); m != redis_data.end(); ++m) {
    auto key = *m;
    m += 1;
    auto value = *m;
    log_debug_formatted(LOGGING_TAG_REDIS, "\t {}: {}", key, value);
    parsed_map[key] = value;
  }
  return parsed_map;
}

std::string gen_model_replica_key(const VersionedModelId& key,
                                  int model_replica_id) {
  std::stringstream ss;
  ss << key.get_name();
  ss << ITEM_DELIMITER;
  ss << key.get_id();
  ss << ITEM_DELIMITER;
  ss << std::to_string(model_replica_id);
  return ss.str();
}

// Expects keys of format "model_name:model_version:replica_id"
std::pair<VersionedModelId, int> parse_model_replica_key(std::string key) {
  size_t pos = key.find(ITEM_DELIMITER);
  if (pos == std::string::npos) {
    throw std::invalid_argument("Couldn't parse model replica key \"" + key +
                                "\"");
  }
  std::string model_name = key.substr(0, pos);
  key.erase(0, pos + ITEM_DELIMITER.length());

  pos = key.find(ITEM_DELIMITER);
  if (pos == std::string::npos) {
    throw std::invalid_argument("Couldn't parse model replica key \"" + key +
                                "\"");
  }
  std::string model_version = key.substr(0, pos);
  key.erase(0, pos + ITEM_DELIMITER.length());
  int replica_id = std::stoi(key);
  VersionedModelId model = VersionedModelId(model_name, model_version);

  return std::make_pair(model, replica_id);
}

std::string gen_versioned_model_key(const VersionedModelId& key) {
  std::stringstream ss;
  ss << key.get_name();
  ss << ":";
  ss << key.get_id();
  return ss.str();
}

std::string gen_model_current_version_key(const std::string& model_name) {
  std::stringstream ss;
  ss << VERSION_METADATA_PREFIX;
  ss << model_name;
  return ss.str();
}

// Update `prohibited_group_strings` when changing the set of delimeters and/or
// other generic substrings used
string labels_to_str(const vector<string>& labels) {
  if (labels.empty()) return "";

  std::ostringstream ss;
  for (auto l = labels.begin(); l != labels.end() - 1; ++l) {
    ss << *l << ITEM_DELIMITER;
  }
  // don't forget to save the last label
  ss << *(labels.end() - 1);
  return ss.str();
}

string model_names_to_str(const vector<string>& names) {
  return labels_to_str(names);
}

// String parsing taken from http://stackoverflow.com/a/14267455/814642
vector<string> str_to_labels(const string& label_str) {
  auto start = 0;
  auto end = label_str.find(ITEM_DELIMITER);
  vector<string> labels;

  while (end != string::npos) {
    labels.push_back(label_str.substr(start, end - start));
    start = end + ITEM_DELIMITER.length();
    end = label_str.find(ITEM_DELIMITER, start);
  }
  // don't forget to parse the last label
  labels.push_back(label_str.substr(start, end - start));
  return labels;
}

std::string models_to_str(const std::vector<VersionedModelId>& models) {
  if (models.empty()) return "";

  std::ostringstream ss;
  for (auto m = models.begin(); m != models.end() - 1; ++m) {
    ss << m->get_name() << ITEM_PART_CONCATENATOR << m->get_id()
       << ITEM_DELIMITER;
  }
  // don't forget to save the last label
  ss << (models.end() - 1)->get_name() << ITEM_PART_CONCATENATOR
     << (models.end() - 1)->get_id();
  log_debug_formatted(LOGGING_TAG_REDIS, "models_to_str result: {}", ss.str());
  return ss.str();
}

std::vector<VersionedModelId> str_to_models(const std::string& model_str) {
  auto start = 0;
  auto end = model_str.find(ITEM_DELIMITER);
  vector<VersionedModelId> models;

  while (end != string::npos) {
    auto split =
        start +
        model_str.substr(start, end - start).find(ITEM_PART_CONCATENATOR);
    std::string model_name = model_str.substr(start, split - start);
    std::string model_version = model_str.substr(split + 1, end - split - 1);
    models.push_back(VersionedModelId(model_name, model_version));
    start = end + ITEM_DELIMITER.length();
    end = model_str.find(ITEM_DELIMITER, start);
  }

  // don't forget to parse the last model
  auto split =
      start + model_str.substr(start, end - start).find(ITEM_PART_CONCATENATOR);
  std::string model_name = model_str.substr(start, split - start);
  std::string model_version = model_str.substr(split + 1, end - split - 1);
  models.push_back(VersionedModelId(model_name, model_version));

  return models;
}

bool set_current_model_version(redox::Redox& redis,
                               const std::string& model_name,
                               const std::string& version) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_METADATA_DB_NUM)})) {
    std::string key = gen_model_current_version_key(model_name);
    const vector<string> cmd_vec{"SET", key, version};

    return send_cmd_no_reply<string>(redis, cmd_vec);
  } else {
    return false;
  }
}

boost::optional<std::string> get_current_model_version(
    redox::Redox& redis, const std::string& model_name) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_METADATA_DB_NUM)})) {
    std::string key = gen_model_current_version_key(model_name);
    auto result = send_cmd_with_reply<string>(redis, {"GET", key});
    if (result) {
      std::string version = *result;
      if (version.size() == 0) {
        log_error_formatted(LOGGING_TAG_REDIS,
                            "Versions cannot be empty string. Found version {}",
                            version);
      } else {
        return version;
      }
    }
  }
  log_error_formatted(LOGGING_TAG_REDIS, "No versions found for model {}",
                      model_name);
  return boost::none;
}

std::vector<std::string> get_linked_models(redox::Redox& redis,
                                           const std::string& app_name) {
  std::vector<std::string> linked_models;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APP_MODEL_LINKS_DB_NUM)})) {
    auto result =
        send_cmd_with_reply<std::vector<string>>(redis, {"SMEMBERS", app_name});
    if (result) {
      linked_models = *result;
    } else {
      log_error_formatted(LOGGING_TAG_REDIS,
                          "Found no linked models for app {}", app_name);
    }
  } else {
    log_error_formatted(
        LOGGING_TAG_REDIS,
        "Redis encountered an error in searching for app links for {}",
        app_name);
  }

  return linked_models;
}

bool add_model(Redox& redis, const VersionedModelId& model_id,
               const InputType& input_type, const vector<string>& labels,
               const std::string& container_name,
               const std::string& model_data_path) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_MODEL_DB_NUM)})) {
    std::string model_id_key = gen_versioned_model_key(model_id);
    // clang-format off
    const vector<string> cmd_vec{
      "HMSET",            model_id_key,
      "model_name",       model_id.get_name(),
      "model_version",    model_id.get_id(),
      "load",             std::to_string(0.0),
      "input_type",       get_readable_input_type(input_type),
      "labels",           labels_to_str(labels),
      "container_name",   container_name,
      "model_data_path",  model_data_path};
    // clang-format on
    return send_cmd_no_reply<string>(redis, cmd_vec);
  } else {
    return false;
  }
}

bool delete_model(Redox& redis, const VersionedModelId& model_id) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_MODEL_DB_NUM)})) {
    std::string model_id_key = gen_versioned_model_key(model_id);
    return send_cmd_no_reply<int>(redis, {"DEL", model_id_key});
  } else {
    return false;
  }
}

unordered_map<string, string> get_model(Redox& redis,
                                        const VersionedModelId& model_id) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_MODEL_DB_NUM)})) {
    std::string model_id_key = gen_versioned_model_key(model_id);

    std::vector<std::string> model_data;
    auto result =
        send_cmd_with_reply<vector<string>>(redis, {"HGETALL", model_id_key});
    if (result) {
      model_data = *result;
    }
    return parse_redis_map(model_data);
  } else {
    return unordered_map<string, string>{};
  }
}

std::vector<std::string> get_model_versions(redox::Redox& redis,
                                            const std::string& model_name) {
  std::vector<std::string> versions;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_MODEL_DB_NUM)})) {
    std::stringstream ss;
    ss << model_name;
    ss << ":*";
    auto key_regex = ss.str();
    auto result =
        send_cmd_with_reply<vector<string>>(redis, {"KEYS", key_regex});
    if (result) {
      std::vector<std::string> model_keys;
      model_keys = *result;
      for (auto model_str : model_keys) {
        std::vector<VersionedModelId> parsed_model = str_to_models(model_str);
        versions.push_back(parsed_model.front().get_id());
      }
    }
  }
  return versions;
}

std::vector<std::string> get_all_model_names(redox::Redox& redis) {
  std::vector<std::string> model_names;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_MODEL_DB_NUM)})) {
    // Use wildcard argument for KEYS command to get all key names.
    // The number of keys is assumed to be within reasonable limits.
    auto result = send_cmd_with_reply<vector<string>>(redis, {"KEYS", "*"});
    if (result) {
      // De-duplicate and return the key names.
      std::set<std::string> model_name_set;
      for (auto model_str : *result) {
        std::vector<VersionedModelId> parsed_model = str_to_models(model_str);
        model_name_set.insert(parsed_model.front().get_name());
      }
      model_names.insert(model_names.end(), model_name_set.begin(),
                         model_name_set.end());
    }
  }
  return model_names;
}

std::vector<VersionedModelId> get_all_models(redox::Redox& redis) {
  std::vector<VersionedModelId> models;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_MODEL_DB_NUM)})) {
    // Use wildcard argument for KEYS command to get all key names.
    // The number of keys is assumed to be within reasonable limits.
    auto result = send_cmd_with_reply<vector<string>>(redis, {"KEYS", "*"});
    if (result) {
      for (auto model_str : *result) {
        std::vector<VersionedModelId> parsed_model = str_to_models(model_str);
        models.push_back(parsed_model.front());
      }
    }
  }
  return models;
}

std::vector<std::string> get_all_backend_names(redox::Redox& redis) {
  std::vector<std::string> empty_names;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_BACKEND_DB_NUM)})) {
    auto result = send_cmd_with_reply<vector<string>>(redis, {"KEYS", "*"});
    if (result) {
      return *result;
    }
  }
  return empty_names;
}

bool add_container(Redox& redis, const VersionedModelId& model_id,
                   const int model_replica_id, const int zmq_connection_id,
                   const InputType& input_type) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_CONTAINER_DB_NUM)})) {
    std::string replica_key = gen_model_replica_key(model_id, model_replica_id);
    std::string model_id_key = gen_versioned_model_key(model_id);
    // clang-format off
    const vector<string> cmd_vec{
      "HMSET",             replica_key,
      "model_id",          model_id_key,
      "model_name",        model_id.get_name(),
      "model_version",     model_id.get_id(),
      "model_replica_id",  std::to_string(model_replica_id),
      "zmq_connection_id", std::to_string(zmq_connection_id),
      "batch_size",        std::to_string(1),
      "input_type",        get_readable_input_type(input_type)};
    // clang-format on
    return send_cmd_no_reply<string>(redis, cmd_vec);
  } else {
    return false;
  }
}

bool delete_container(Redox& redis, const VersionedModelId& model_id,
                      const int model_replica_id) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_CONTAINER_DB_NUM)})) {
    std::string replica_key = gen_model_replica_key(model_id, model_replica_id);
    return send_cmd_no_reply<int>(redis, {"DEL", replica_key});
  } else {
    return false;
  }
}

unordered_map<string, string> get_container(Redox& redis,
                                            const VersionedModelId& model_id,
                                            const int model_replica_id) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_CONTAINER_DB_NUM)})) {
    std::string replica_key = gen_model_replica_key(model_id, model_replica_id);
    std::vector<std::string> container_data;
    auto result =
        send_cmd_with_reply<vector<string>>(redis, {"HGETALL", replica_key});
    if (result) {
      container_data = *result;
    }
    return parse_redis_map(container_data);
  } else {
    return unordered_map<string, string>{};
  }
}

unordered_map<string, string> get_container_by_key(Redox& redis,
                                                   const std::string& key) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_CONTAINER_DB_NUM)})) {
    std::vector<std::string> container_data;
    auto result = send_cmd_with_reply<vector<string>>(redis, {"HGETALL", key});
    if (result) {
      container_data = *result;
    }
    return parse_redis_map(container_data);
  } else {
    return unordered_map<string, string>{};
  }
}

std::vector<std::pair<VersionedModelId, int>> get_all_containers(
    redox::Redox& redis) {
  std::vector<std::pair<VersionedModelId, int>> containers;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_CONTAINER_DB_NUM)})) {
    // Use wildcard argument for KEYS command to get all key names.
    // The number of keys is assumed to be within reasonable limits.
    auto result = send_cmd_with_reply<vector<string>>(redis, {"KEYS", "*"});
    if (result) {
      auto container_keys = *result;
      for (auto c : container_keys) {
        containers.push_back(parse_model_replica_key(c));
      }
    }
  }
  return containers;
}

bool add_application(redox::Redox& redis, const std::string& appname,
                     const InputType& input_type, const std::string& policy,
                     const std::string& default_output,
                     const long latency_slo_micros) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APPLICATION_DB_NUM)})) {
    // clang-format off
    const vector<string> cmd_vec{
      "HMSET",              appname,
      "input_type",         get_readable_input_type(input_type),
      "policy",             policy,
      "default_output",     default_output,
      "latency_slo_micros", std::to_string(latency_slo_micros)
    };
    // clang-format on
    return send_cmd_no_reply<string>(redis, cmd_vec);
  } else {
    return false;
  }
}

std::pair<bool, std::string> add_retrain_data(
    redox::Redox& redis, const long timestamp,
    const std::vector<std::vector<double>>& data_list) {
  std::string data_id = gen_retrain_data_id(timestamp);
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_RETRAIN_DATA_DB)})) {
    for (auto data : data_list) {
      std::vector<std::string> cmd_str;
      std::transform(data.begin(), data.end(), std::back_inserter(cmd_str),
                     [](const double ele) { return std::to_string(ele); });
      cmd_str.push_back(ITEM_DELIMITER);

      cmd_str.insert(cmd_str.begin(), data_id);
      cmd_str.insert(cmd_str.begin(), "RPUSH");

      if (!send_cmd_no_reply<int>(redis, cmd_str)) {
        return std::make_pair(false, data_id);
      }
    }
    return std::make_pair(true, data_id);
  } else {
    return std::make_pair(false, "Unknow Database");
  }
}

// TODO modify the gen_id mechanism
std::string gen_retrain_data_id(const long timestamp) {
  std::random_device rd;
  std::stringstream ss;
  ss << std::to_string(timestamp);
  ss << std::to_string(rd() % 1000);
  return ss.str();
}

bool add_app_data_link(redox::Redox& redis, const std::string& app_name,
                       const long timestamp, const std::string& data_id) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APP_DATA_LINK_DB)})) {
    std::stringstream ss;
    ss << app_name;
    ss << ITEM_DELIMITER;
    ss << std::to_string(timestamp);
    std::string key = ss.str();
    // clang-format off
    const vector<string> cmd_vec{
      "HMSET",              key,
      "app_name",           app_name,
      "timestamp",          std::to_string(timestamp),
      "data_id",            data_id
    };
    // clang-format on
    return send_cmd_no_reply<string>(redis, cmd_vec);
  } else {
    return false;
  }
}

// borrow from
// https://stackoverflow.com/questions/9435385/split-a-string-using-c11
std::vector<std::string> split(const std::string& s, char delim) {
  std::stringstream ss(s);
  std::string item;
  std::vector<std::string> elems;
  while (std::getline(ss, item, delim)) {
    // elems.push_back(item);
    elems.push_back(std::move(item));
  }
  return elems;
}

std::vector<std::string> get_retrain_data_ids(redox::Redox& redis,
                                              const std::string& app_name,
                                              const long begin_timestamp,
                                              const long end_timestamp) {
  std::vector<std::string> data_ids;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APP_DATA_LINK_DB)})) {
    std::stringstream ss;
    ss << app_name;
    ss << ITEM_DELIMITER;
    ss << "*";
    std::string all_key_with_name = ss.str();

    auto result =
        send_cmd_with_reply<vector<string>>(redis, {"KEYS", all_key_with_name});
    if (result) {
      for (auto key : *result) {
        long arrive_timestamp = std::stol(split(key, ',')[1]);
        if (arrive_timestamp <= end_timestamp &&
            arrive_timestamp >= begin_timestamp) {
          auto data_id =
              send_cmd_with_reply<string>(redis, {"HGET", key, "data_id"});
          if (data_id) {
            data_ids.push_back(*data_id);
          }
        }
      }
    }
  }
  return data_ids;
}

bool add_backend(Redox& redis, const std::string& backend_name,
                 const std::string& backend_version, const std::string& policy,
                 const double alpha, const double beta, const double weight,
                 const int zmq_connection_id) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_BACKEND_DB_NUM)})) {
    // clang-format off
    const vector<string> cmd_vec{
      "HMSET",             backend_name,
      "backend_version",   backend_version,
      "policy",            policy,
      "zmq_connection_id", std::to_string(zmq_connection_id),
      "alpha",             std::to_string(alpha),
      "beta",              std::to_string(beta),
      "weight",            std::to_string(weight)
    };
    // clang-format on
    return send_cmd_no_reply<string>(redis, cmd_vec);
  } else {
    return false;
  }
}

std::unordered_map<std::string, std::string> get_backend(
    redox::Redox& redis, const std::string& backend_name) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_BACKEND_DB_NUM)})) {
    std::vector<std::string> backend_data;
    auto result =
        send_cmd_with_reply<vector<string>>(redis, {"HGETALL", backend_name});
    if (result) {
      backend_data = *result;
    }

    return parse_redis_map(backend_data);
  } else {
    return unordered_map<string, string>{};
  }
}

bool set_backend_link(redox::Redox& redis, const std::string& app_name,
                      const std::string& backend_name) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APP_BACKEND_LINK_DB_NUM)})) {
    return send_cmd_no_reply<string>(redis, {"SET", app_name, backend_name});
  } else {
    return false;
  }
}

std::string get_backend_link(redox::Redox& redis, const std::string& app_name) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APP_BACKEND_LINK_DB_NUM)})) {
    auto result = send_cmd_with_reply<string>(redis, {"GET", app_name});
    if (result) {
      return *result;
    }
  }
  return "";
}

bool add_model_links(redox::Redox& redis, const std::string& appname,
                     const std::vector<std::string>& model_names) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APP_MODEL_LINKS_DB_NUM)})) {
    for (auto model_name : model_names) {
      if (!send_cmd_no_reply<int>(
              redis, vector<string>{"SADD", appname, model_name})) {
        return false;
      }
    }
    return true;
  } else {
    return false;
  }
}

bool delete_application(redox::Redox& redis, const std::string& appname) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APPLICATION_DB_NUM)})) {
    return send_cmd_no_reply<int>(redis, {"DEL", appname});
  } else {
    return false;
  }
}

std::unordered_map<std::string, std::string> get_application(
    redox::Redox& redis, const std::string& appname) {
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APPLICATION_DB_NUM)})) {
    std::vector<std::string> container_data;
    auto result =
        send_cmd_with_reply<vector<string>>(redis, {"HGETALL", appname});
    if (result) {
      container_data = *result;
    }

    return parse_redis_map(container_data);
  } else {
    return unordered_map<string, string>{};
  }
}

std::unordered_map<std::string, std::string> get_application_by_key(
    redox::Redox& redis, const std::string& key) {
  // Applications just use their appname as a key.
  // We keep the get_*_by_key() to preserve the symmetry of the
  // API.
  return get_application(redis, key);
}

std::vector<string> get_all_application_names(redox::Redox& redis) {
  std::vector<std::string> app_names;
  if (send_cmd_no_reply<string>(
          redis, {"SELECT", std::to_string(REDIS_APPLICATION_DB_NUM)})) {
    // Use wildcard argument for KEYS command to get all key names.
    // The number of keys is assumed to be within reasonable limits.
    auto result = send_cmd_with_reply<vector<string>>(redis, {"KEYS", "*"});
    if (result) {
      app_names = *result;
    }
  }
  return app_names;
}

void subscribe_to_keyspace_changes(
    int db, std::string prefix, Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  std::ostringstream subscription;
  subscription << "__keyspace@" << std::to_string(db) << "__:" << prefix << "*";
  std::string sub_str = subscription.str();
  log_debug_formatted(LOGGING_TAG_REDIS, "SUBSCRIPTION STRING: {}", sub_str);
  subscriber.psubscribe(sub_str, [callback, prefix](const std::string& topic,
                                                    const std::string& msg) {
    size_t split_idx = topic.find_first_of(":");
    std::string key = topic.substr(split_idx + 1 + prefix.size());
    log_debug_formatted(LOGGING_TAG_REDIS, "MESSAGE: {}", msg);
    callback(key, msg);
  });
}

void subscribe_to_model_changes(
    Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  std::string prefix = "";
  subscribe_to_keyspace_changes(REDIS_MODEL_DB_NUM, prefix, subscriber,
                                std::move(callback));
}

void subscribe_to_container_changes(
    Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  std::string prefix = "";
  subscribe_to_keyspace_changes(REDIS_CONTAINER_DB_NUM, prefix, subscriber,
                                std::move(callback));
}

void subscribe_to_application_changes(
    redox::Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  std::string prefix = "";
  subscribe_to_keyspace_changes(REDIS_APPLICATION_DB_NUM, prefix, subscriber,
                                std::move(callback));
}

void subscribe_to_backend_link_changes(
    redox::Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  std::string prefix = "";
  subscribe_to_keyspace_changes(REDIS_APP_BACKEND_LINK_DB_NUM, prefix, subscriber,
                                std::move(callback));
}

void subscribe_to_model_link_changes(
    redox::Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  std::string prefix = "";
  subscribe_to_keyspace_changes(REDIS_APP_MODEL_LINKS_DB_NUM, prefix,
                                subscriber, std::move(callback));
}

void subscribe_to_model_version_changes(
    redox::Subscriber& subscriber,
    std::function<void(const std::string&, const std::string&)> callback) {
  subscribe_to_keyspace_changes(REDIS_METADATA_DB_NUM, VERSION_METADATA_PREFIX,
                                subscriber, std::move(callback));
}

}  // namespace redis
}  // namespace continuum
