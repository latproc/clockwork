
PIDCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT Kp, Ki, Kd;

	OPTION UpdateTime 20; # minimum time between normal control updates
	OPTION RampIncLimit 1000;		# maximum increase control power per cycle
	OPTION RampDecLimit -1000; # maximum decrease of control power per cycle

	OPTION Kp 100;
	OPTION Ki 200;
	OPTION Kd 0;
}

PIDSTATE MACHINE Control, Process, PIDCalc {
	OPTION Err 0;
	OPTION ErrSum 0;
	OPTION ErrRate 0; 
	OPTION Output 0;
	
	OPTION SumLimitHigh 0;
	OPTION SumLimitLow 0; 
	
	PRIVATE OPTION de 0;
	PRIVATE OPTION lastErr 0; 
	PRIVATE OPTION t 0;
	PRIVATE OPTION lastT 0;
	
	COMMAND Reset { Err := 0; ErrSum := 0; ErrRate := 0 }
	COMMAND Update {
		Err := Control.VALUE - Process.VALUE;
		ErrSum := ErrSum + Err;
		IF (ErrSum > SumLimitHigh) { errSum := SumLimitHigh }
		IF (ErrSum < SumLimitLow) { errSum := SumLimitLow }
		t := Control.IOTIME;
		dt := t - lastT;
		lastT := t;
		ErrRate := (Err - lastErr) / dt;
		lastErr := Err;
		LOCK PIDCalc;
		COPY PROPERTIES Err, ErrSum, ErrRate TO PIDCalc;
		CALL PIDCalc.update;
		Output := PIDCalc.Out;
		UNLOCK PIDCalc; 
	}
}                                                 git@github.com:mleadbeater/soap-app-dev.git

PIDCALC MACHINE config {

OPTION Err 0;
OPTION ErrSum 0;
OPTION ErrRate 0;
OPTION Out 0;

COMMAND Update {
   Out := Err * config.Kp + ErrSum * config.Ki + ErrRate * config.Kd;
}

}