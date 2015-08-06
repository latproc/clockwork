
PLC MODBUSMODULE(host:"localhost", port:1502);
I_BaleOnLoader INPUTBIT PLC;
I_BaleAtLoaderPos INPUTBIT PLC;
I_LoaderUp INPUTBIT PLC;
I_LoaderBlockOn INPUTBIT PLC;
I_PanelBlockIgnore INPUTBIT PLC;
I_X33BlockIgnore INPUTBIT PLC;

O_LoaderBlock OUTPUTBIT PLC;
M_WeightAvailable OUTPUTBIT PLC;

RawScales MACHINE {
	OPTION rawWeight 0;
	OPTION rawDecWeight 0;
	OPTION rawUnderWeight 0;
	OPTION rawOverWeight 0;
	OPTION rawSteady 0;
	OPTION message "test message";

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
