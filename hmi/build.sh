#/bin/sh

gcc -o panel -I /usr/local/include/modbus -L/usr/local/lib -lmodbus  panel.c
g++ -o modbus_sample -I /usr/local/include -I/usr/local/include/modbus modbus_sample.cxx \
	`fltk-config --ldflags` -L/usr/local/lib -lmodbus -lboost_thread -lboost_system
