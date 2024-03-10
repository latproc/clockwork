FROM ubuntu:20.04 as builder

RUN apt update
ENV TZ=Australia/Adelaide
RUN apt install -y tzdata
RUN apt install -y \
	wget bison build-essential \
	subversion git flex libboost-all-dev autoconf libtool pkg-config vim screen \
	libreadline-dev libmosquitto-dev cmake

COPY . /src
WORKDIR /src
RUN cd /tmp
RUN	/usr/bin/bash /src/scripts/build_ubnt_debian_cw.sh
RUN cd /src/iod && make release-install
RUN ldconfig

ENV PATH=/src/iod:$PATH
