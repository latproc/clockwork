g++ -dynamiclib -I /usr/local/include -I../iod date.cpp globals.cpp -L/usr/local/lib -lboost_system -o date.so lib/*.o -lzmq -lmosquitto -lboost_filesystem
