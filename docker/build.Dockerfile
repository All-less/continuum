FROM allless/continuum-base:latest

LABEL "maintainer" "All-less<all.less.mail@gmail.com>"

WORKDIR /opt/continuum

RUN git pull \
    && bash bin/init_dev_environment.sh -b

ENTRYPOINT /bin/bash
CMD []
