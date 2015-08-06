
PLC MODBUSMODULE(host:"localhost", port:1502);
I_BaleOnLoader INPUTBIT(export:rw) PLC;
I_BaleAtLoaderPos INPUTBIT(export:rw) PLC;
I_LoaderUp INPUTBIT(export:rw) PLC;
I_LoaderBlockOn INPUTBIT(export:rw) PLC;
I_PanelBlockIgnore INPUTBIT(export:rw) PLC;
I_X33BlockIgnore INPUTBIT(export:rw) PLC;

O_LoaderBlock OUTPUTBIT(export:ro) PLC;
M_WeightAvailable OUTPUTBIT(export:ro) PLC;

RawScales MACHINE {
	OPTION rawWeight 0;
	OPTION rawDecWeight 0;
	OPTION rawUnderWeight 0;
	OPTION rawOverWeight 0;
	OPTION rawSteady 0;
	OPTION message "test message";
	EXPORT READWRITE STRING 40 message;
	EXPORT RW 16BIT rawWeight, rawDecWeight, rawSteady, rawUnderWeight, rawOverWeight;

}
M_rawScales RawScales;

BaleCounter MACHINE bale_present {
	OPTION count 0;
	OPTION PERSISTENT "true";
	
	Ready WHEN bale_present IS on;
	Idle DEFAULT;

	TRANSITION Ready TO Idle USING Count;
	COMMAND Count { count := count + 1; }
}
bale_counter BaleCounter I_BaleOnLoader;
