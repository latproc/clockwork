echo /usr/local/lib > /etc/ld.so.conf.d/custom.conf ; ldconfig

apt-add-repository ppa:mosquitto-dev/mosquitto-ppa
apt-get update

apt-get install python-software-properties bison build-essential \
  python-software-properties subversion git flex libboost-all-dev \
  autoconf libtool pkg-config vim screen libreadline-dev \
  libmosquitto-dev libsodium-dev libcurl4-openssl-dev

[ ! -r v3.0.4.tar.gz ] && wget https://github.com/stephane/libmodbus/archive/v3.0.4.tar.gz
#wget http://download.zeromq.org/zeromq-3.2.3.tar.gz
[ ! -r zeromq-4.3.2.tar.gz ] && wget https://github.com/zeromq/libzmq/releases/download/v4.3.2/zeromq-4.3.2.tar.gz
[ ! -r zmq.hpp ] && wget https://github.com/zeromq/cppzmq/raw/master/zmq.hpp

[ ! -d libmodbus-3.0.4 ] && tar -zxf v3.0.4.tar.gz
[ ! -d zeromq-4.3.2 ] && tar -zxf zeromq-4.3.2.tar.gz

# Build Lib Modbus
cd libmodbus-3.0.4/
./autogen.sh
./configure
make && make install

cd ../

# Build ZeroMQ
cd zeromq-4.3.2/
./configure
make && make install
cd ../

# Put the c++ headers for ZeroMQ into the right place 
cp zmq.hpp /usr/local/include/


