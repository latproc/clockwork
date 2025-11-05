FlasherOne MACHINE { 
  OPTION on_time 1000;
	inactive INITIAL; inactive WHEN SELF IS inactive;
	on WHEN SELF IS on AND TIMER < on_time OR SELF IS off AND TIMER>=on_time;
	off DEFAULT;
}
