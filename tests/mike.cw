xRampSettingsTemplate MACHINE { OPTION outputPower 0; OPTION minValue 0; OPTION rampUp 300; }
xSettingsTemplate MACHINE { OPTION MinValue 0;  OPTION MaxValue 10000; }
xSettings xSettingsTemplate;
xRampSettings xRampSettingsTemplate;
GuardMachine MACHINE { true STATE; false STATE; }
xGuard GuardMachine;
ActionFlag MACHINE { on STATE; delayOff STATE; off INITIAL; COMMAND turnOn { WAIT 100; } COMMAND turnOff { WAIT 100; } }
xAction ActionFlag;
xCommonAction ActionFlag;
xMyAction FLAG;
xActions LIST xCommonAction, xMyAction;
xSelector FLAG;
xPWMEnable FLAG;
xOutput FLAG(VALUE:0);


x ANALOGDEMANDGUARD(ramp:"off") xSettings, xRampSettings, xGuard, xAction, xCommonAction, xActions, xSelector, xPWMEnable, xOutput;

# Config property needed for ramping ramp:on
ANALOGDEMANDGUARD MACHINE Settings, RampSettings, Guard, R_Action, R_CommonAction, L_Actions, Selector, PWMEnable, Output {
  OPTION VALUE 0;
  OPTION onTime 0;
  LOCAL OPTION tmp 0;
  Ramp ANALOGRAMP RampSettings, OWNER;
  Update FLAG;

  interlocked WHEN Output DISABLED;
  interlocked WHEN R_Action IS off && (ANY L_Actions ARE on || ANY L_Actions ARE delayOff) && R_CommonAction != delayOff;
  interlocked WHEN Guard != true;

  on WHEN R_Action IS on && Guard IS true && Selector IS on && PWMEnable IS on,
    TAG Update WHEN R_Action IS on && Output.VALUE != VALUE + Settings.MinValue;
  off_shared WHEN R_Action IS delayOff;
  off WHEN R_Action IS off;

  LEAVE on { onTime := TIMER; }

  RECEIVE Update.on_enter {
    tmp := 0;
    IF (VALUE > 0) { tmp := VALUE + Settings.MinValue; };
    IF (tmp > Settings.MaxValue) { tmp := Settings.MaxValue; };
    Output.VALUE := tmp;
  }
  COMMAND turnOn { CALL turnOn ON R_Action; }
  COMMAND turnOff { CALL turnOff ON R_Action; VALUE := 0; Output.VALUE := VALUE; }
  RECEIVE noop { CALL turnOff ON R_Action; VALUE := 0; Output.VALUE := VALUE; }
  TRANSITION ANY TO on USING turnOn REQUIRES SELF IS NOT interlocked;
  TRANSITION on TO off USING turnOff REQUIRES SELF IS NOT interlocked;
  TRANSITION ANY TO interlocked USING noop;
}


ANALOGRAMP MACHINE S_Ramp, Parent {
  LOCAL OPTION ramp "off";
  LOCAL OPTION VALUE 0;
  LOCAL OPTION startTime 0;
  LOCAL OPTION timeDelta 0;
  LOCAL OPTION ratio 0;
  LOCAL OPTION baseRange 0;
  externalControl FLAG; updateRamp FLAG;

  disable WHEN Parent.ramp != "on";
  on WHEN Parent IS on,
    TAG updateRamp WHEN updateRamp IS off && updateRamp.TIMER >= 10 && externalControl IS off;
  off DEFAULT;
  off INITIAL;

  ENTER on {
    baseRange := S_Ramp.outputPower - S_Ramp.minValue;
    Parent.VALUE := S_Ramp.minValue;
    startTime := NOW;
  }

  ENTER off { SET externalControl TO off; }

  RECEIVE updateRamp.on_enter {
    timeDelta := NOW - startTime;
    ratio := timeDelta * 10 / S_Ramp.rampUp;
    IF ( ratio < 10 ) {
      VALUE := baseRange * ratio / 10 + S_Ramp.minValue;
    } ELSE {
      VALUE := S_Ramp.outputPower;
      SET externalControl TO on;
    };
    Parent.VALUE := VALUE;
  }
}

