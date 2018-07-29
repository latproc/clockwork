
prox1 FLAG;
prox2 FLAG;
prox3 FLAG;
L_CoreProx LIST prox1, prox2, prox3;

CorePos MACHINE sensors {
	OPTION pos 0;
	idle DEFAULT;

	update WHEN SELF IS changing AND TIMER >= 20;
	changing WHEN BITSET FROM sensors != pos;
	one WHEN pos == 4;
	two WHEN pos == 6;
	three WHEN pos == 7;
	four WHEN pos == 5;
	five WHEN pos == 3;
	six WHEN pos == 2;

	ENTER update { 
		pos := BITSET FROM sensors;
	}
}

IA_corePos CorePos L_CoreProx ;
