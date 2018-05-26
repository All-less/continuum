#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/exception_ptr.hpp>

#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>

#include <continuum/config.hpp>
#include <continuum/constants.hpp>
#include <continuum/data_processor.hpp>
#include <continuum/datatypes.hpp>
#include <continuum/exceptions.hpp>
#include <continuum/json_util.hpp>
#include <continuum/logging.hpp>
#include <continuum/redis.hpp>

#include <server_http.hpp>

using continuum::json::json_parse_error;
using continuum::json::json_semantic_error;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

namespace data_frontend {

const std::string LOGGING_TAG_DATA_FRONTEND = "DATAFRONTEND";
const std::string DATA_UPLOAD = "^/.*/upload$";

const std::string UPDATE_JSON_SCHEMA = R"(
  {
   "data" := [[double]]
  }
)";

// TODO refactor
void respond_http(std::string content, std::string message,
                  std::shared_ptr<HttpServer::Response> response) {
  *response << "HTTP/1.1 " << message << "\r\nContent-Type: application/json"
            << "\r\nContent-Length: " << content.length() << "\r\n\r\n"
            << content << "\n";
}

std::string json_error_msg(const std::string& exception_msg,
                           const std::string& expected_schema) {
  std::stringstream ss;
  ss << "Error parsing JSON: " << exception_msg << ". "
     << "Expected JSON schema: " << expected_schema;
  return ss.str();
}

template <class DP>
class RequestHandler {
 public:
  RequestHandler(std::string address, int port, int num_threads)
      : server_(address, port, num_threads), data_processor_() {
    continuum::Config& conf = continuum::get_config();
    while (!redis_connection_.connect(conf.get_redis_address(),
                                      conf.get_redis_port())) {
      continuum::log_error(LOGGING_TAG_DATA_FRONTEND,
                         "Data frontend failed to connect to Redis.",
                         "Retrying in 1 second...");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!redis_subscriber_.connect(conf.get_redis_address(),
                                      conf.get_redis_port())) {
      continuum::log_error(LOGGING_TAG_DATA_FRONTEND,
                         "Data frontend subscriber failed to connect to Redis.",
                         "Retrying in 1 second...");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    continuum::redis::subscribe_to_application_changes(
        redis_subscriber_,
        [this](const std::string& key, const std::string& event_type) {
          continuum::log_debug_formatted(
              LOGGING_TAG_DATA_FRONTEND,
              "Application event detected. key:{} event_type:{}", key,
              event_type);
          if (event_type == "hset") {
            std::string name = key;
            continuum::log_info_formatted(LOGGING_TAG_DATA_FRONTEND,
                                        "New application detected: {}.", key);
            add_data_upload_endpoint(name);
            add_retrain_endpoint(name);
          }
        });
  }

  ~RequestHandler() {
    redis_connection_.disconnect();
    redis_subscriber_.disconnect();
  }

  void start_listening() { server_.start(); }

  void add_retrain_endpoint(std::string name) {
    std::string retrain_url = "^/" + name + "/retrain$";
    server_.add_endpoint(
        retrain_url, "POST",
        [this, name](std::shared_ptr<HttpServer::Response> response,
                     __attribute__((unused)) std::shared_ptr<HttpServer::Request> request) {
          try {
            continuum::log_info_formatted(LOGGING_TAG_DATA_FRONTEND,
                                        "Received manual trigger. app:{}",
                                        name);
            folly::Future<long> res = data_processor_.manual_retrain(name);

            res.then([response](long size) {
              std::stringstream ss;
              ss << "Retrain data size : " << size << "\n";
              std::string content = ss.str();
              respond_http(content, "200 OK", response);
            });

          } catch (const std::invalid_argument& e) {
            respond_http(e.what(), "400 Bad Request", response);
          }
        });
  }

  void add_data_upload_endpoint(std::string name) {
    std::string data_upload_url = "^/" + name + "/upload$";
    server_.add_endpoint(
        data_upload_url, "POST",
        [this, name](std::shared_ptr<HttpServer::Response> response,
                     std::shared_ptr<HttpServer::Request> request) {
          try {
            folly::Future<bool> upload =
                decode_and_handle_upload(request->content.string(), name);

            upload.then([response](bool ack) {
              std::stringstream ss;
              ss << "Upload received? " << ack << "\n";
              std::string content = ss.str();
              respond_http(content, "200 OK", response);
            });

          } catch (const json_parse_error& e) {
            std::string error_msg =
                json_error_msg(e.what(), UPDATE_JSON_SCHEMA);
            respond_http(error_msg, "400 Bad Request", response);
          } catch (const json_semantic_error& e) {
            std::string error_msg =
                json_error_msg(e.what(), UPDATE_JSON_SCHEMA);
            respond_http(error_msg, "400 Bad Request", response);
          } catch (const std::invalid_argument& e) {
            respond_http(e.what(), "400 Bad Request", response);
          }
        });
  }

  /*
   * JSON format for uploading data requests:
   * {
   *  "data" := [[double]]
   * }
   */
  folly::Future<bool> decode_and_handle_upload(std::string json_content,
                                               std::string name) {
    rapidjson::Document d;
    continuum::json::parse_json(json_content, d);

    long current_time_micros =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    auto upload_data = continuum::json::get_double_arrays(d, "data");
    auto add_data_result = continuum::redis::add_retrain_data(
        redis_connection_, current_time_micros, upload_data);

    if (add_data_result.first) {
      continuum::redis::add_app_data_link(
          redis_connection_, name, current_time_micros, add_data_result.second);

      continuum::log_info_formatted(LOGGING_TAG_DATA_FRONTEND,
                                  "Received new data. app:{} size:{}",
                                  name, upload_data.size());

      return data_processor_.update_retrain_trigger_data(
          name, current_time_micros, upload_data.size());
    } else {
      return folly::makeFuture(false);
    }
  }

 private:
  HttpServer server_;
  DP data_processor_;
  redox::Redox redis_connection_;
  redox::Subscriber redis_subscriber_;
};

}  // namespace data_frontend
