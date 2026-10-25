FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential libseccomp-dev python3 openjdk-17-jre-headless \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
