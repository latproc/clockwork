# simulation of circuit gates

NAND_GATE MACHINE in1, in2 {
  off WHEN in1 IS on AND in2 IS on;
  on DEFAULT;
}

AND_GATE MACHINE in1, in2 {
  on WHEN in1 IS on AND in2 IS on;
  off DEFAULT;
}

AND3_GATE MACHINE in1, in2, in3 {
  on WHEN in1 IS on AND in2 IS on AND in3 IS on;
  off DEFAULT;
}

NAND3_GATE MACHINE in1, in2, in3 {
  and3 AND3_GATE in1, in2, in3;
  on WHEN and3 IS off;
  off DEFAULT;
}

NOT_GATE MACHINE in {
  off WHEN in IS on;
  on DEFAULT;
}

OR_GATE MACHINE in1, in2 {
  off WHEN in1 IS off AND in2 IS off;
  on DEFAULT;
}

NOR_GATE MACHINE in1, in2 {
  on WHEN in1 IS off AND in2 IS off;
  off DEFAULT;
}

XOR_GATE MACHINE in1, in2 {
  on WHEN in1 IS on AND in2 IS off OR in1 IS off AND in2 IS on;
  off DEFAULT;
}

# Count how many bits are set in the inputs
BitCounter MACHINE in1, in2, in3, in4 {
  xor1 XOR_GATE in1, in2;
  xor2 XOR_GATE in3, in4;
  xor3 XOR_GATE xor1, xor2;

  xor4 XOR_GATE in1, in2;
  xor5 XOR_GATE in3, in4;
  xor6 XOR_GATE xor4, xor5;
  nxo6 NOT_GATE xor6;
  or1 OR_GATE in1, in2;
  or2 OR_GATE in3, in4;
  or3 OR_GATE or1, or2;
  and1 AND_GATE nxo6, or3;

  and31 AND3_GATE in1, in2, in3;
  and32 AND3_GATE in1, in2, in4;
  and33 AND3_GATE in1, in3, in4;
  and34 AND3_GATE in2, in3, in4;
  or4 OR_GATE and31, and32;
  or5 OR_GATE and33, and34;
  or6 OR_GATE or4, or5;

  or7 OR_GATE and1, or6;

  and2 AND_GATE and32, and34;
  not1 NOT_GATE and2;
  and3 AND_GATE or7, not1;

  zero WHEN xor3 IS off AND and3 IS off AND and2 IS off;
  one WHEN xor3 IS on AND and3 IS off AND and2 IS off;
  two WHEN xor3 IS off AND and3 IS on AND and2 IS off;
  three WHEN xor3 IS on AND and3 IS on AND and2 IS off;
  four WHEN xor3 IS off AND and3 IS off AND and2 IS on;

  OPTION VALUE 0;
  ENTER zero { VALUE := 0; }
  ENTER one { VALUE :=  1; }
  ENTER two { VALUE := 2; }
  ENTER three { VALUE := 3; }
  ENTER four { VALUE := 4; }

  unknown DEFAULT;
  ENTER unknown { VALUE := -1; }
}

b1 FLAG;
b2 FLAG;
b3 FLAG;
b4 FLAG;
bits LIST b1, b2, b3, b4;

count BitCounter b1, b2, b3, b4;

