ErrorSettings MACHINE {
    OPTION ErrSumLimit 0;
    OPTION Kff 0.005;
    OPTION Kp 0.001;
    OPTION Ki 0.005;
    OPTION Kd 0.0001;
}

Sim MACHINE {
    LOCAL OPTION input 0.0;
    OPTION value 0.0;
    LOCAL OPTION last_input 0.0;
    LOCAL OPTION last 0.0;

    update LOCAL STATE;
    idle LOCAL STATE;

    update WHEN SELF IS idle AND TIMER >= 10;
    idle DEFAULT;

    ENTER update {
        last_input := input;
        last := value;
        value := input * 100.0 + (RANDOM % 10 / 100.0);
    }
}

PIDCONTROL MACHINE motor{

    Settings ErrorSettings(ErrSumLimit:1500);

    LOCAL OPTION Err 0.0;
    LOCAL OPTION ErrSum 0.0;
    LOCAL OPTION ErrRate 0.0;
    LOCAL OPTION target 0;
    LOCAL OPTION last_target 0;
    LOCAL OPTION last_error 0.0;
    LOCAL OPTION control_value 0;

    LOCAL OPTION sample_time_ms 100;

    reset WHEN last_target != target;
    update LOCAL STATE;
    idle LOCAL STATE;

    update WHEN SELF IS idle AND TIMER >= sample_time_ms;
    idle DEFAULT;

    ENTER reset {
        ErrSum := 0.0;
        ErrRate := 0.0;
        last_target := target;
        last_error := 0.0;
        SEND recalc TO SELF;
    }

    ENTER update {
        SEND recalc TO SELF;
        motor.input := control_value;
    }

    RECEIVE recalc {
        # calc PID
        Err := target - motor.value;
        ErrSum := ErrSum + Err;
        ErrRate := (Err - last_error) / (sample_time_ms / 1000.0);

        # limit error sum
        IF(ABS ErrSum > ABS Settings.ErrSumLimit) {
            ErrSum := Settings.ErrSumLimit * ABS ErrSum / ErrSum;
        };
        control_value := Settings.Kff * target
                       + Settings.Kp * Err
                       + Settings.Ki * ErrSum
                       - Settings.Kd * ErrRate;
    }
}

motor Sim;
control PIDCONTROL(target: 0) motor;


