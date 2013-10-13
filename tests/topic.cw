
M2 MQTTBROKER "127.0.0.1", 1883;

Switch POINT (tab:test) M2, "Mega1/dig/7", "off";
Light POINT (tab:test) M2, "Mega1/dig/3";

I_LightLevel SUBSCRIBER(tab:test) M2, "Mega1/ana/3";
O_Dial PUBLISHER(tab:test) M2, "Mega1/pwm/11", 0;

sense Sensor(tab:test) I_LightLevel;

Adjuster MACHINE sensor, dial {
    OPTION output_level 0;

    waiting WHEN TIMER < 200 OR SELF IS dark OR SELF IS light;
    dark WHEN sensor IS dark && output_level <250;
    light WHEN sensor IS light && output_level > 0;
    idle WHEN sensor IS just_right;
    overrun DEFAULT;
    
    ENTER dark { 
        output_level := output_level + 1;
        dial.message := output_level;
    }
    
    ENTER light {
        output_level := output_level - 1;
        dial := output_level;
    }
}
adjuster Adjuster(tab:test) sense, O_Dial;

#
Sensor MACHINE level {
    OPTION light_level 0;
    OPTION target_level 0;
    current_light LEVEL(tab:test);
    
    dark WHEN light_level < target_level - 2;
    light WHEN light_level > target_level + 2;
    just_right DEFAULT;
    
    RECEIVE level.property_change { 
        light_level := level.message;
        current_light.position := level.message;
    }
}


LEVEL MACHINE {
	OPTION position 0;
    OPTION maxpos 400;
    OPTION type "piston";
}

