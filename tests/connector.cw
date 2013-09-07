# device connector tests (explanation TBD)

Sample MACHINE {
    OPTION message "";
    ready STATE;
}
sampler Sample(tab:Test);

Monitor MACHINE device {
    OPTION last_message "";
    update WHEN SELF IS idle AND device.message != last_message;
    idle DEFAULT;
    ENTER update { 
        last_message := device.message;
        LOG "updated";
    }
    ENTER INIT { SET device TO ready; }
}
monitor Monitor (tab:Test), sampler;
