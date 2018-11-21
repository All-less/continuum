## Continuum

A platform for online learning that curtails data latency and saves you cost. For the design information, please refer to the paper [Continuum: A Platform for Cost-Aware, Low-Latency Continual Learning](https://dl.acm.org/citation.cfm?id=3267817).

### Technical Preview

WARNING: The project is in pre-alpha state. Please use with caution.

It is recommended to run Continuum with [Docker 18.03+](https://docs.docker.com/release-notes/docker-ce/). Following is a basic walkthrough.

```bash
git clone https://github.com/All-less/continuum.git
cd continuum

# start a local swarm cluster
docker swarm init

# deploy frontends
docker stack deploy -c docker/docker-compose-frontend.yml continuum-frontend

# register application "test-app"
curl -X POST \                                    
        --header "Content-Type:application/json" \
        -d '{"name":"test-app", "input_type":"doubles", "default_output":"-1.0", "latency_slo_micros":100000 }' \
        http://0.0.0.0:1338/admin/add_app
  
# deploy backends
docker stack deploy -c docker/docker-compose-backend.yml continuum-backend

# send data to perform retrain
curl -X POST \
        --header "Content-Type:application/json" \
        -d '{"data":[[1.0, 2.0]]}'\
        http://0.0.0.0:1339/test-app/upload

```

### Acknowledgement

The project is inspired by [Clipper](http://clipper.ai/).
