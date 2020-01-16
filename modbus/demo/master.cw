# Modbus sample 


#PLC MODBUSMODULE(host:"127.0.0.1", port:2502);
PLC MODBUSMODULE;
INT0 INPUTREGISTER(type: "SignedInt") PLC, "W17400", 1; # test signed integer -12345
#INT1 INPUTREGISTER PLC, "W17401", 2;
#INT2 INPUTREGISTER(format: "Float", length: 2) PLC, "W17401", 2;
F1 INPUTREGISTER(format: "Float", address: "4:0x1f01", length: 2) PLC, "test", 2;
#INT3 INPUTREGISTER PLC, "W5354", 1;
#INT4 INPUTREGISTER PLC, "W5355", 1;
#INT5 INPUTREGISTER PLC, "W5356", 1;

PLCSTATUS MACHINE {
  OPTION status "";
}

FloatRegister MACHINE a,b {
	LOCAL OPTION value 0;

	recalc WHEN value != (a.VALUE & 0xff) & 65536 + (b.VALUE & 0xffff);
	idle DEFAULT;
	ENTER recalc { 
		value := (a.VALUE & 0xff) * 65536 + (b.VALUE & 0xffff);
	}
}
#F1 FloatRegister INT1, INT2;
#F2 FloatRegister INT2, INT1;

connection_status PLCSTATUS;
