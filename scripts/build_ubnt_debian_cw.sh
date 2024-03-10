ZMQ_VERSION=4.3.3

echo /usr/local/lib > /etc/ld.so.conf.d/custom.conf ; ldconfig

apt-get update

apt-get install -y \
	wget bison build-essential \
	subversion git flex libboost-all-dev autoconf libtool pkg-config vim screen \
	libreadline-dev libmosquitto-dev cmake
err=$?
if [ $? -ne 0 ]; then
	echo "Error installing packages. please resolve before continuing"
	exit 1
fi

wget https://github.com/stephane/libmodbus/archive/v3.0.4.tar.gz
if expr "$ZMQ_VERSION" < "4.1.4"; then
	wget http://download.zeromq.org/zeromq-${ZMQ_VERSION}.tar.gz || exit 1
elif [ "$ZMQ_VERSION" = "4.1.5" ]; then
	wget https://github.com/zeromq/zeromq4-1/releases/download/v${ZMQ_VERSION}/zeromq-${ZMQ_VERSION}.tar.gz || exit 1
else
	wget https://github.com/zeromq/libzmq/releases/download/v${ZMQ_VERSION}/zeromq-${ZMQ_VERSION}.tar.gz || exit 1
fi
wget https://github.com/zeromq/cppzmq/raw/master/zmq.hpp

tar -zxf v3.0.4.tar.gz
tar -zxf zeromq-${ZMQ_VERSION}.tar.gz

# Build Lib Modbus
cd libmodbus-3.0.4/
./autogen.sh
./configure
make && make install

cd ../

# Build ZeroMQ
cd zeromq-${ZMQ_VERSION}/
./configure
make && make install
cd ..

# Put the c++ headers for ZeroMQ into the right place 
cp zmq.hpp /usr/local/include/


