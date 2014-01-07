echo /usr/local/lib > /etc/ld.so.conf.d/custom.conf ; ldconfig

apt-add-repository ppa:mosquitto-dev/mosquitto-ppa
apt-get update

apt-get install python-software-properties bison build-essential python-software-properties subversion git flex libboost-all-dev autoconf libtool pkg-config vim screen libreadline-dev libmosquitto-dev

wget https://github.com/stephane/libmodbus/archive/v3.0.4.tar.gz
wget http://download.zeromq.org/zeromq-3.2.3.tar.gz
wget https://github.com/zeromq/cppzmq/raw/master/zmq.hpp

tar -zxf v3.0.4.tar.gz
tar -zxf zeromq-3.2.3.tar.gz

# Build Lib Modbus
cd libmodbus-3.0.4/
./autogen.sh
./configure
make && make install

cd ../

# Build ZeroMQ
cd zeromq-3.2.3/
./configure
make && make install

# Put the c++ headers for ZeroMQ into the right place 
mv zmq.hpp /usr/local/include/


