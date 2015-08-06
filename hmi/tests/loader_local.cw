
PLC MODBUSMODULE(host:"localhost", port:1502);
I_BaleOnLoader INPUTBIT PLC, "X1";
I_BaleAtLoaderPos INPUTBIT PLC, "X2";
I_LoaderUp INPUTBIT PLC, "X3";
I_LoaderBlockOn INPUTBIT PLC, "X4";
I_PanelBlockIgnore INPUTBIT PLC, "X5";
I_X33BlockIgnore INPUTBIT PLC, "X6";

O_LoaderBlock OUTPUTBIT PLC, "Y1";
M_WeightAvailable OUTPUTBIT PLC, "Y2";

RawWeight INPUTREGISTER PLC, "V1";
RawDecWeight INPUTREGISTER PLC, "V2";
RawUnderWeight INPUTREGISTER PLC, "V3";
RawOverWeight INPUTREGISTER PLC, "V4";
RawSteady INPUTREGISTER PLC, "V5";
Message INPUTREGISTER PLC, "V6", 40;

/*
RawScales MACHINE {
	OPTION rawWeight 0;
	OPTION rawDecWeight 0;
	OPTION rawUnderWeight 0;
	OPTION rawOverWeight 0;
	OPTION rawSteady 0;
	OPTION message "test message";

}
M_rawScales RawScales;
*/

BaleCounter MACHINE bale_present {
	OPTION count 0;
	OPTION PERSISTENT "true";
	
	Ready WHEN bale_present IS on;
	Idle DEFAULT;

	TRANSITION Ready TO Idle USING Count;
	COMMAND Count { count := count + 1; }
}
bale_counter BaleCounter I_BaleOnLoader;
