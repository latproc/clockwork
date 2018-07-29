#
# first install dependencies

brew install cmake
brew install zmq
brew install zmqpp
brew install libmodbus
#brew install bison
brew install boost
brew install pkgconfig
brew install mosquitto

# clone clockwork
git clone https://github.com/latproc/clockwork.git

# clone cppzmq - a c++ interface for zmq
# this is got get the header file 'zmq.hpp'
git clone https://github.com/zeromq/cppzmq.git

#------------

# side-journey: installing the zmq.hpp header file...
#     see below if you wish to take a short-cut

# the support files bundled with cppzmq don't seem to find
# the brew installed version of clockwork so we copy
# a helper from the clockwork directory first
cd cppzmq && cp ../clockwork/iod/cmake/Modules/FindZeroMQ.cmake .

# standard cmake build pattern
mkdir build && (cd build && cmake .. && make install)
cd ..

# optionally use the latest libzmq instead of the brew installed
# one. This isn't needed unless there are some problems after the
# above steps
# git clone https://github.com/zeromq/libzmq.git
# brew uninstall libzmq
# cd libzmq && mkdir build && (cd build && cmake .. && make install)

# ---- short-cut
# try:
# cp cppzmq/zmq.hpp /usr/local/include
#------------


# build clockwork
cd clockwork && make release-install

