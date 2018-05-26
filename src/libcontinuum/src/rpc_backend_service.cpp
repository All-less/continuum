#include <boost/bimap.hpp>
#include <boost/functional/hash.hpp>

#include <chrono>
#include <iostream>

#include <redox.hpp>

#include <continuum/config.hpp>
#include <continuum/datatypes.hpp>
#include <continuum/json_util.hpp>
#include <continuum/logging.hpp>
#include <continuum/metrics.hpp>
#include <continuum/redis.hpp>
#include <continuum/rpc_backend_service.hpp>
#include <continuum/util.hpp>
#include <continuum/constants.hpp>

using std::shared_ptr;
using std::string;
using std::vector;
using zmq::context_t;
using zmq::message_t;
using zmq::socket_t;

namespace continuum {

namespace rpc {

/**
 * RPC service for communicating with retrain backends.
 */
RPCBackendService::RPCBackendService()
    : request_queue_(std::make_shared<Queue<RPCRequest>>()),
      response_queue_(std::make_shared<Queue<RPCBackendResponse>>()),
      active_(false) {
  msg_queueing_hist_ = metrics::MetricsRegistry::get_metrics().create_histogram(
      "internal:rpc_request_queueing_delay", "microseconds", 2056);
}

RPCBackendService::~RPCBackendService() { stop(); }

void RPCBackendService::start(
    const string ip, const int port,
    std::function<void(RPCBackendResponse)> &&retrain_started_callback,
    std::function<void(RPCBackendResponse)> &&retrain_finished_callback) {
  retrain_started_callback_ = retrain_started_callback;
  retrain_finished_callback_ = retrain_finished_callback;
  if (active_) {
    throw std::runtime_error(
        "Attempted to start RPC Service when it is already running!");
  }
  const string address = "tcp://" + ip + ":" + std::to_string(port);
  active_ = true;
  rpc_thread_ = std::thread([this, address]() { manage_service(address); });
}

void RPCBackendService::stop() {
  if (active_) {
    active_ = false;
    rpc_thread_.join();
  }
}

int RPCBackendService::send_message(const vector<vector<uint8_t>> msg,
                                    const int zmq_connection_id) {
  if (!active_) {
    log_error(LOGGING_TAG_RPC_BACKEND,
              "Cannot send message to inactive RPCBackendService instance",
              "Dropping Message");
    return -1;
  }
  int id = message_id_;
  message_id_ += 1;
  long current_time_micros =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  RPCRequest request(zmq_connection_id, id, std::move(msg),
                     current_time_micros);
  request_queue_->push(request);
  return id;
}

vector<RPCBackendResponse> RPCBackendService::try_get_responses(
    const int max_num_responses) {
  vector<RPCBackendResponse> responses;
  for (int i = 0; i < max_num_responses; i++) {
    if (auto response = response_queue_->try_pop()) {
      responses.push_back(*response);
    } else {
      break;
    }
  }
  return responses;
}

void RPCBackendService::manage_service(const string address) {
  log_info_formatted(LOGGING_TAG_RPC_BACKEND,
                     "Backend RPC thread started. address: {}", address);
  boost::bimap<int, vector<uint8_t>> connections;

  std::unordered_map<std::vector<uint8_t>, std::string,
                     std::function<size_t(const std::vector<uint8_t> &vec)>>
      connections_backend_map(INITIAL_REPLICA_ID_SIZE, hash_vector<uint8_t>);
  context_t context = context_t(1);
  socket_t socket = socket_t(context, ZMQ_ROUTER);
  socket.bind(address);
  // Indicate that we will poll our zmq service socket for new inbound messages
  zmq::pollitem_t items[] = {{socket, 0, ZMQ_POLLIN, 0}};
  int zmq_connection_id = 0;
  auto redis_connection = std::make_shared<redox::Redox>();
  Config &conf = get_config();
  while (!redis_connection->connect(conf.get_redis_address(),
                                    conf.get_redis_port())) {
    log_error(LOGGING_TAG_RPC_BACKEND,
              "RPCBackendService failed to connect to Redis.",
              "Retrying in 1 second...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  while (active_) {
    // Set poll timeout based on whether there are outgoing messages to
    // send. If there are messages to send, don't let the poll block at all.
    // If there no messages to send, let the poll block for 1 ms.
    int poll_timeout = 0;
    if (request_queue_->size() == 0) {
      poll_timeout = 1;
    }
    zmq_poll(items, 1, poll_timeout);
    if (items[0].revents & ZMQ_POLLIN) {
      // TODO: Balance message sending and receiving fairly
      // Note: We only receive one message per event loop iteration
      log_debug(LOGGING_TAG_RPC_BACKEND, "Found message to receive.");

      receive_message(socket, connections, connections_backend_map,
                      zmq_connection_id, redis_connection);
    }
    // Note: We send all queued messages per event loop iteration
    send_messages(socket, connections);
  }
  shutdown_service(socket);
}

void RPCBackendService::shutdown_service(socket_t &socket) {
  size_t buf_size = 32;
  std::vector<char> buf(buf_size);
  socket.getsockopt(ZMQ_LAST_ENDPOINT, (void *)buf.data(), &buf_size);
  std::string last_endpoint = std::string(buf.begin(), buf.end());
  socket.unbind(last_endpoint);
  socket.close();
}

void RPCBackendService::send_messages(
    socket_t &socket, boost::bimap<int, vector<uint8_t>> &connections) {
  while (request_queue_->size() > 0) {
    long current_time_micros =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    RPCRequest request = request_queue_->pop();
    msg_queueing_hist_->insert(current_time_micros - std::get<3>(request));
    boost::bimap<int, vector<uint8_t>>::left_const_iterator connection =
        connections.left.find(std::get<0>(request));
    if (connection == connections.left.end()) {
      // Error handling
      log_error_formatted(LOGGING_TAG_CONTINUUM,
                          "Attempted to send message to unknown backend: ",
                          std::get<0>(request));
      continue;
    }

    message_t type_message(sizeof(int));
    static_cast<int *>(type_message.data())[0] =
        static_cast<int>(MessageType::StartRetraining);
    message_t id_message(sizeof(int));
    memcpy(id_message.data(), &std::get<1>(request), sizeof(int));
    vector<uint8_t> routing_identity = connection->second;

    socket.send(routing_identity.data(), routing_identity.size(), ZMQ_SNDMORE);
    socket.send("", 0, ZMQ_SNDMORE);
    socket.send(type_message, ZMQ_SNDMORE);
    socket.send(id_message, ZMQ_SNDMORE);
    int cur_msg_num = 0;
    // subtract 1 because we start counting at 0
    int last_msg_num = std::get<2>(request).size() - 1;
    for (const std::vector<uint8_t> &m : std::get<2>(request)) {
      // send the sndmore flag unless we are on the last message part
      if (cur_msg_num < last_msg_num) {
        socket.send((uint8_t *)m.data(), m.size(), ZMQ_SNDMORE);
      } else {
        socket.send((uint8_t *)m.data(), m.size(), 0);
      }
      cur_msg_num += 1;
    }
  }
}

void RPCBackendService::receive_message(
    socket_t &socket, boost::bimap<int, vector<uint8_t>> &connections,

    std::unordered_map<std::vector<uint8_t>, std::string,
                       std::function<size_t(const std::vector<uint8_t> &vec)>>
        &connections_backend_map,
    int &zmq_connection_id, std::shared_ptr<redox::Redox> redis_connection) {
  message_t msg_routing_identity;
  message_t msg_delimiter;
  message_t msg_type;
  socket.recv(&msg_routing_identity, 0);
  socket.recv(&msg_delimiter, 0);
  socket.recv(&msg_type, 0);

  const vector<uint8_t> connection_id(
      (uint8_t *)msg_routing_identity.data(),
      (uint8_t *)msg_routing_identity.data() + msg_routing_identity.size());

  MessageType type =
      static_cast<MessageType>(static_cast<int *>(msg_type.data())[0]);

  boost::bimap<int, vector<uint8_t>>::right_const_iterator connection =
      connections.right.find(connection_id);
  bool new_connection = (connection == connections.right.end());
  switch (type) {
    case MessageType::BackendMetadata: {
      message_t backend_name;
      message_t backend_version;
      message_t app_name;
      message_t policy_name;
      message_t params_msg;
      socket.recv(&backend_name, 0);
      socket.recv(&backend_version, 0);
      socket.recv(&app_name, 0);
      socket.recv(&policy_name, 0);
      socket.recv(&params_msg, 0);

      if (new_connection) {
        // We have a new connection with backend metadata, process it
        // accordingly
        connections.insert(boost::bimap<int, vector<uint8_t>>::value_type(
            zmq_connection_id, connection_id));
        std::string name(static_cast<char *>(backend_name.data()),
                         backend_name.size());
        std::string version(static_cast<char *>(backend_version.data()),
                            backend_version.size());
        std::string app(static_cast<char *>(app_name.data()), app_name.size());
        std::string policy(static_cast<char *>(policy_name.data()),
                           policy_name.size());
        std::string params(static_cast<char *>(params_msg.data()),
                           params_msg.size());
        rapidjson::Document d;
        json::parse_json(params, d);
        auto alpha = json::try_get_double(d, "alpha", DEFAULT_ALPHA);
        auto beta = json::try_get_double(d, "beta", DEFAULT_BETA);
        auto weight = json::try_get_double(d, "weight", DEFAULT_WEIGHT);

        redis::add_backend(*redis_connection, name, version, policy,
                           alpha, beta, weight, zmq_connection_id);

        log_info_formatted(LOGGING_TAG_RPC_BACKEND,
                           "New backend connected. backend:{} app:{} "
                           "alpha:{} beta:{} policy:{} weight:{}",
                           name, app, alpha, beta, policy, weight);

        auto app_name_vec = redis::get_all_application_names(*redis_connection);
        auto itr = std::find(app_name_vec.begin(), app_name_vec.end(), app);
        if (itr != app_name_vec.end()) {
          redis::set_backend_link(*redis_connection, app, name);
        }
        connections_backend_map.emplace(connection_id, name);

        zmq_connection_id += 1;
      }
    } break;
    case MessageType::RetrainingStarted: {
      // This message is a response to a retrain query
      message_t msg_id;
      message_t retrain_result;
      socket.recv(&msg_id, 0);
      socket.recv(&retrain_result, 0);
      if (!new_connection) {
        int id = static_cast<int *>(msg_id.data())[0];
        int result = static_cast<int *>(retrain_result.data())[0];

        RPCBackendResponse response(id, result);
        log_debug_formatted(
            LOGGING_TAG_RPC_BACKEND,
            "Received RetrainingStarted. msg_id: {} result: {}", id,
            result);
        auto backend_name = connections_backend_map.find(connection_id);
        if (backend_name == connections_backend_map.end()) {
          throw std::runtime_error(
              "Failed to find backend that was previously registered via "
              "RPC");
        }

        retrain_started_callback_(response);
        // response_queue_->push(response);
      }
    } break;
    case MessageType::BackendHeartbeat:
      send_heartbeat_response(socket, connection_id, new_connection);
      break;
    case MessageType::RetrainingEnded: {
      // Message id is the same with "StartRetrain" message
      // TODO refactor
      message_t msg_id;
      message_t retrain_result;
      socket.recv(&msg_id, 0);
      socket.recv(&retrain_result, 0);
      if (!new_connection) {
        int id = static_cast<int *>(msg_id.data())[0];
        int result = static_cast<int *>(retrain_result.data())[0];

        RPCBackendResponse response(id, result);
        log_debug_formatted(
            LOGGING_TAG_RPC_BACKEND,
            "Received RetrainingEnded. msg_id: {} result: {}", id,
            result);

        auto backend_name = connections_backend_map.find(connection_id);
        if (backend_name == connections_backend_map.end()) {
          throw std::runtime_error(
              "Failed to find backend that was previously registered via "
              "RPC");
        }

        retrain_finished_callback_(response);
      }
    } break;
    default:
      log_error_formatted(LOGGING_TAG_RPC_BACKEND,
                          "Received message with unrecognized type.");
      break;
  }
}

void RPCBackendService::send_heartbeat_response(
    socket_t &socket, const vector<uint8_t> &connection_id,
    bool request_backend_metadata) {
  message_t type_message(sizeof(int));
  message_t heartbeat_type_message(sizeof(int));
  static_cast<int *>(type_message.data())[0] =
      static_cast<int>(MessageType::BackendHeartbeat);
  static_cast<int *>(heartbeat_type_message.data())[0] = static_cast<int>(
      // In the scope, "request container metadata" equals to request metadata
      // of backend, which requires backend sends MessageType::BackendMetadata
      request_backend_metadata ? HeartbeatType::RequestContainerMetadata
                               : HeartbeatType::KeepAlive);

  socket.send(connection_id.data(), connection_id.size(), ZMQ_SNDMORE);
  socket.send("", 0, ZMQ_SNDMORE);
  socket.send(type_message, ZMQ_SNDMORE);
  socket.send(heartbeat_type_message);

  log_debug(LOGGING_TAG_RPC_BACKEND, "Sent heartbeat.");
}

}  // namespace rpc

}  // namespace continuum
