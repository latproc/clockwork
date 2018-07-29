Invert MACHINE flag {
	OPTION count 0;
	on WHEN SELF IS on AND flag IS off;
	on WHEN flag IS off;
	off DEFAULT;
	COMMAND reset { count := 0; SET SELF TO off; }
}
xx Invert f;
f FLAG;
