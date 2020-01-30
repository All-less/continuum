FROM allless/continuum-base:0.1

LABEL "maintainer" "All-less<all.less.mail@gmail.com>"

WORKDIR /opt/continuum

RUN git pull \
    && bash bin/init_dev_environment.sh -b

ENTRYPOINT /bin/bash
CMD []
