MODULES {
  EK1100_00 (position:0); # Console Bus Coupler
  EL1088_01 (position:1); # 8 in 3ms neg switching
  EL1088_02 (position:2); # 8 in 3ms neg switching
  EL1088_03 (position:3); # 8 in 3ms neg switching
  EL1088_04 (position:4); # 8 in 3ms neg switching
  EL2088_05 (position:5); # 8 out 0.5A
  EL2088_06 (position:6); # 8 out 0.5A
  EK1814_07 (position:7); # Eight Core Bus Coupler
  EL1098_08 (position:8); # 8 in 10us neg switching
  EL1098_09 (position:9); # 8 in 10us neg switching
  EL1018_10 (position:10); # 8 In 10us pos switching
  EL2889_11 (position:11); # 16 out 0.5A 0v ground
  EL2088_12 (position:12); # 8 out 0.5A 0v ground
  EL2024_13 (position:13); # 4 out 2A pos
}

a POINT EK1814_07, 0;
b POINT EK1814_07, 1;
c POINT EK1814_07, 2;
d POINT EK1814_07, 3;
e POINT EK1814_07, 4;
f POINT EK1814_07, 5;
g POINT EK1814_07, 6;
h POINT EK1814_07, 7;

/*
sync: 0 pdo: 0 Channel 5:  entry: 0{7080, 1, 1}
sync: 0 pdo: 1 Channel 6:  entry: 0{7090, 1, 1}
sync: 0 pdo: 2 Channel 7:  entry: 0{70a0, 1, 1}
sync: 0 pdo: 3 Channel 8:  entry: 0{70b0, 1, 1}
sync: 1 pdo: 0 Channel 1:  entry: 0{6000, 1, 1}
sync: 1 pdo: 1 Channel 2:  entry: 0{6010, 1, 1}
sync: 1 pdo: 2 Channel 3:  entry: 0{6020, 1, 1}
sync: 1 pdo: 3 Channel 4:  entry: 0{6030, 1, 1}
EK1814 EtherCAT-EA-Koppler (0,5A E-Bus, 4 K. Dig. Ein, 3ms, 4 K: 0, 72, 7162c52
Domain Registation: 0, 1, 2, 4403052, 6000, 1 offset addr 0x21bfbb0
Domain Registation: 0, 2, 2, 4403052, 6000, 1 offset addr 0x21c07d0
Domain Registation: 0, 3, 2, 4403052, 6000, 1 offset addr 0x21c14c0
Domain Registation: 0, 4, 2, 4403052, 6000, 1 offset addr 0x21c21f0
Domain Registation: 0, 5, 2, 8283052, 7000, 1 offset addr 0x21c2f60
Domain Registation: 0, 6, 2, 8283052, 7000, 1 offset addr 0x21c3d30
Domain Registation: 0, 7, 2, 7162c52, 7080, 1 offset addr 0x21c4c50
Domain Registation: 0, 7, 2, 7162c52, 6000, 1 offset addr 0x21c4c54
PDO entry registration succeeded
*/


