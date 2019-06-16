This directory contains some simple clockwork programs that are used
as examples for the generation of C code from clockwork.

to run them, first compile the clockwork interpreter 

  cd latproc/clockwork/iod
  make

then run the interpreter passing this directory and an option to trigger the export:

  build/cw --export_c ../examples/esp32/

files will be written to /tmp/cw_export
