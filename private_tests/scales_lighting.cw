SCALESLIGHTING MACHINE Power, Reset, Light {
 OPTION PERSISTENT true;
 OPTION lightOffDelay 600000;

 reset STATE;
 timeout WHEN SELF IS on && TIMER >= lightOffDelay;
 on WHEN SELF IS on;
 off DEFAULT;

 ENTER reset { SET SELF TO on; }

 RECEIVE Reset.on_enter { SET SELF TO reset; }
 RECEIVE Power.on_enter { SET SELF TO reset; }

 ENTER on { SET Light TO on; }
 LEAVE on { SET Light TO off; }
 ENTER off { SET Light TO off; }

}

power FLAG;
reset FLAG;
light FLAG;
lighting SCALESLIGHTING power, reset, light;

