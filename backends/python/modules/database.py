# coding: utf-8
import redis

from .constants import *


class RedisService:
    def __init__(self, host, port, db=REDIS_RETRAIN_DATA_DB):
        self.host = host
        self.port = port
        self.db = db
        self._pool = redis.ConnectionPool(
            host=self.host, port=self.port, db=self.db)
        self.redis_connect = redis.Redis(connection_pool=self._pool)

    def get_data_by_ids(self, data_ids):
        """
        An alternative:

            get_doubles = lambda id_: self.convert_str_to_doubles(self.redis_connect.lrange(id_, 0, -1))
            raw_data = [ get_doubles(id_) for id_ in data_ids ]
            return reduce(operator.add, raw_data)
        """
        datas = []
        for id_ in data_ids:
            raw_data = self.redis_connect.lrange(id_, 0, -1)
            datas = datas + self.convert_str_to_doubles(raw_data)
        return datas

    def convert_str_to_doubles(self, raw_data):
        """
        An alternative:

            return [ float(d) for d in raw_data.split(ITEM_DELIMITER) ]
        """
        data_list = []
        data = []
        for ele in raw_data:  # can we really handle double by iterating over characters?
            if ele == ITEM_DELIMITER:
                data_list.append(list(data))
                data = []
            else:
                data.append(float(ele))
        return data_list


# test if it works
# the data list 1 and 2 are presaved in redis
if __name__ == '__main__':
    redis = RedisService(REDIS_HOST, REDIS_PORT)
    print(redis.get_data_by_ids(["1", "2"]))
