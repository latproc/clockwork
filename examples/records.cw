Colour RECORD {
  r BYTE;
  g BYTE;
  b BYTE;
  pos { x INT16; y INT16; };
  vel { x INT16; y INT16; };
  y | g | b;
  z { y | g | b; }
}



