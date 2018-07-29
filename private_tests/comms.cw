cw2 CLOCKWORK (HOST:"localhost", PORT: 6000);

I_LoaderUp EXTERNAL cw2, I_LoaderUp;
O_LoaderDown EXTERNAL cw2, C_LoaderDown;
Error EXTERNAL cw2, M_Error;

Test MACHINE in, out {

	TurningOn WHEN in IS on; 
	ENTER TurningOn { SET out TO on; }

	turnOff WHEN in IS off;
	ENTER TurningOff { SET out TO off; }

}
test Test I_LoaderUp, O_LoaderDown;

Logger MACHINE error {
	OPTION last "";
	logging WHEN last != error.cause;

	ENTER logging { 
		last := error.cause;
		LOG last;
	}
}
logger Logger Error;

