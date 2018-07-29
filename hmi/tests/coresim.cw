F_CoreScalesSteady FLAG(export:ro);
I_CoreLoaderAtPos FLAG(export:ro);
I_CoreLoaderBalePresent FLAG(export:ro);
I_CutterBalePresent FLAG(export:ro);
I_FeederBalePresent FLAG(export:ro);
I_GrabBaleAtBaleGate FLAG(export:ro);
I_GrabBalePresent FLAG(export:ro);
I_InsertBalePresent FLAG(export:ro);
O_CoreLoaderUp FLAG(export:ro);
O_CoreLoaderUpBlock FLAG(export:rw); 
O_GrabBaleGateBlock FLAG(export:rw); 
O_GrabBaleGateUp FLAG(export:ro);
O_InsertForward FLAG(export:ro);
V_CoreKeepAlive COUNTER(export:reg);
V_CoreScalesCapturedWeight VARIABLE(export:reg32) 0;
V_CoreScalesLiveWeight VARIABLE(export:reg32) 0;
V_GrabBaleGateBaleNo VARIABLE(export:reg) 0;
V_GrabBaleGateFolioSize VARIABLE(export:reg) 0;


COUNTER MACHINE {
	OPTION max 9999;
	OPTION VALUE 0;

	count WHEN VALUE<max AND TIMER>10;
	idle DEFAULT;
	ENTER count{ VALUE := VALUE + 1; }
}
