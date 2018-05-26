#ifndef CONTINUUM_LIB_CONSTANTS_HPP
#define CONTINUUM_LIB_CONSTANTS_HPP

#include <string>
#include <utility>

namespace continuum {

enum RedisDBTable {
  REDIS_STATE_DB_NUM = 1,
  REDIS_MODEL_DB_NUM = 2,
  REDIS_CONTAINER_DB_NUM = 3,
  REDIS_RESOURCE_DB_NUM = 4,
  REDIS_APPLICATION_DB_NUM = 5,
  REDIS_METADATA_DB_NUM = 6,  // used to store Continuum configuration metadata
  REDIS_APP_MODEL_LINKS_DB_NUM = 7,
  REDIS_BACKEND_DB_NUM = 8,
  REDIS_APP_BACKEND_LINK_DB_NUM = 9,
  REDIS_RETRAIN_DATA_DB = 10,
  REDIS_APP_DATA_LINK_DB = 11,
};

constexpr int RPC_SERVICE_PORT = 7000;

constexpr int RPC_BACKEND_SERVICE_PORT = 7001;

constexpr int QUERY_FRONTEND_PORT = 1337;
constexpr int MANAGEMENT_FRONTEND_PORT = 1338;
constexpr int DATA_FRONTEND_PORT = 1339;

const std::string ITEM_DELIMITER = ",";

// used to concatenate multiple parts of an item, such as the
// name and version of a VersionedModelID
const std::string ITEM_PART_CONCATENATOR = ":";

const std::string LOGGING_TAG_CONTINUUM = "Continuum";

constexpr int DEFAULT_USER_ID = 0;

constexpr double DEFAULT_ALPHA = 1.0;
constexpr double DEFAULT_BETA = 1.0;
constexpr double DEFAULT_WEIGHT = 10.0;

}  // namespace continuum
#endif
