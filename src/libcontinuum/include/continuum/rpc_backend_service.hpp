#ifndef CONTINUUM_RPC_BACKEND_SERVICE_HPP
#define CONTINUUM_RPC_BACKEND_SERVICE_HPP

#include <list>
#include <queue>
#include <string>
#include <vector>

#include <boost/bimap.hpp>
#include <zmq.hpp>

#include <continuum/containers.hpp>
#include <continuum/datatypes.hpp>
#include <continuum/metrics.hpp>
#include <continuum/redis.hpp>
#include <continuum/util.hpp>

using std::list;
using std::shared_ptr;
using std::string;
using std::vector;
using zmq::socket_t;

namespace continuum {

namespace rpc {

const std::string LOGGING_TAG_RPC_BACKEND = "RPC_BACKEND";

/// Tuple of zmq_connection_id, message_id, vector of messages, creation time
using RPCRequest =
    std::tuple<const int, const int, const std::vector<std::vector<uint8_t>>,
               const long>;
/// Pair of message_id, result
using RPCBackendResponse = std::pair<const int, const int>;

class RPCBackendService {
 public:
  explicit RPCBackendService();
  ~RPCBackendService();
  // Disallow copy
  RPCBackendService(const RPCBackendService &) = delete;
  RPCBackendService &operator=(const RPCBackendService &) = delete;
  vector<RPCBackendResponse> try_get_responses(const int max_num_responses);

  void start(
      const string ip, const int port,
      std::function<void(RPCBackendResponse)> &&retrain_started_callback,
      std::function<void(RPCBackendResponse)> &&retrain_finished_callback);

  void stop();

  int send_message(const std::vector<std::vector<uint8_t>> msg,
                   const int zmq_connection_id);

 private:
  void manage_service(const string address);
  void send_messages(socket_t &socket,
                     boost::bimap<int, vector<uint8_t>> &connections);

  void receive_message(
      socket_t &socket, boost::bimap<int, vector<uint8_t>> &connections,

      std::unordered_map<std::vector<uint8_t>, std::string,
                         std::function<size_t(const std::vector<uint8_t> &vec)>>
          &connections_backend_map,
      int &zmq_connection_id, std::shared_ptr<redox::Redox> redis_connection);

  void send_heartbeat_response(socket_t &socket,
                               const vector<uint8_t> &connection_id,
                               bool request_backend_metadata);

  void shutdown_service(socket_t &socket);
  std::thread rpc_thread_;
  shared_ptr<Queue<RPCRequest>> request_queue_;
  shared_ptr<Queue<RPCBackendResponse>> response_queue_;
  // Flag indicating whether rpc service is active
  std::atomic_bool active_;
  // The next available message id
  int message_id_ = 0;
  std::shared_ptr<metrics::Histogram> msg_queueing_hist_;

  std::function<void(RPCBackendResponse)> retrain_started_callback_;
  std::function<void(RPCBackendResponse)> retrain_finished_callback_;
};

}  // namespace rpc

}  // namespace continuum

#endif  // CONTINUUM_RPC_BACKEND_SERVICE_HPP
