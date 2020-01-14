# Modbus configuration for HMP sensors

PLCSTATUS MACHINE {
  OPTION status "";
}

connection_status PLCSTATUS;

HMP MODBUSMODULE;

HMP_MONITOR CHANNEL {
    OPTION host "localhost";
    OPTION port 7701;
    MONITORS MACHINES LINKED TO HMP;
    PUBLISHER;
}


# Note: input regisiters have register address and length (number of 16 bit words)

# Test registers
testreg_int   INPUTREGISTER(type: "SignedInt") 		HMP, "TEST0", 1; # test value: -12345
testreg_float INPUTREGISTER(format: "Float", length: 2) HMP, "TEST1", 2; # constant test value: -123.45


#RelativeHumidity		INPUTREGISTER(format: "Float", length: 2) HMP, "M0", 2; # relative humidity
Temperature				INPUTREGISTER(format: "Float", length: 2) HMP, "M2", 2; # relative humidity
#DewPointTemperature		INPUTREGISTER(format: "Float", length: 2) HMP, "M6", 2; # relative humidity
#FrostPointTemperature	INPUTREGISTER(format: "Float", length: 2) HMP, "M8", 2; # relative humidity
#FrostPtTemp1Atm			INPUTREGISTER(format: "Float", length: 2) HMP, "M10", 2; # relative humidity
#DewPointTemp1Atm		INPUTREGISTER(format: "Float", length: 2) HMP, "M12", 2; # relative humidity
#AbsoluteHumidity		INPUTREGISTER(format: "Float", length: 2) HMP, "M14", 2; # relative humidity
#MixingRatio				INPUTREGISTER(format: "Float", length: 2) HMP, "M16", 2; # relative humidity
#WetBulbTemperature		INPUTREGISTER(format: "Float", length: 2) HMP, "M18", 2; # relative humidity
#WaterConcentration		INPUTREGISTER(format: "Float", length: 2) HMP, "M20", 2; # relative humidity
#WaterVapourPressure		INPUTREGISTER(format: "Float", length: 2) HMP, "M22", 2; # relative humidity
#WaterVapourSaturationPres	INPUTREGISTER(format: "Float", length: 2) HMP, "M24", 2; # relative humidity
#Enthalpy				INPUTREGISTER(format: "Float", length: 2) HMP, "M26", 2; # relative humidity
#DewPtTemperatureDiff	INPUTREGISTER(format: "Float", length: 2) HMP, "M30", 2; # relative humidity
#AbsoluteHumidityNTP		INPUTREGISTER(format: "Float", length: 2) HMP, "M32", 2; # relative humidity
#WaterMassFraction		INPUTREGISTER(format: "Float", length: 2) HMP, "M64", 2; # relative humidity

# Configuration registers (HMP7 only)
Heater OUTPUTREGISTER HMP, "M0x508", 1;

# Communication registers
SerialId OUTPUTREGISTER HMP, "M0x600", 1;

# Calibration registers
#TODO

