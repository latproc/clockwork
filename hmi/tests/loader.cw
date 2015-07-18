
I_BaleOnLoader FLAG(export:rw);
I_BaleAtLoaderPos FLAG(export:rw);
I_LoaderUp FLAG(export:rw);
I_LoaderBlockOn FLAG(export:rw);
I_PanelBlockIgnore FLAG(export:rw);
I_X33BlockIgnore FLAG(export:rw);

O_LoaderBlock FLAG(export:ro);
M_WeightAvailable FLAG(export:ro);

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
