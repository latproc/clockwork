
MotorPower FLAG(PLC:Y0, Size:1);
PowerAvailable FLAG(PLC:X1, Size:1);
MotorOn FLAG(PLC:X1, Size:1);
IndexPosition VARIABLE(PLC:V0, Size:16) 0;
CycleCount VARIABLE(PLC:W0, Size:32) 0;

PLCChannl CHANNEL {
	MONITORS MACHINES WITH PROPERTY PLC;
	OPTION host "localhost"; OPTION port 7921;
}

