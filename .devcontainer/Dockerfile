FROM ubuntu:latest

RUN apt-get update && \
    apt-get install -y 

RUN sed -i 's/^# \(.*export LS_OPTIONS.*$\)/\1/g' ~/.bashrc && \
    sed -i 's/^# \(.*alias ll.*$\)/\1/g' ~/.bashrc

WORKDIR /app
COPY .github/install_dependencies /app/
RUN /app/install_dependencies
RUN apt-get install git -y
RUN apt-get clean && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
