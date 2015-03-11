#ifndef __COMMANDS_H__
#define __COMMANDS_H__

void press(Fl_Widget *w, void *data);

const int M_GrabBaleManager_Load = 4281;
const int M_GrabBaleManager_Unload = 4282;
const int M_GrabBaleManager_Reposition = 4283;
const int M_GrabBaleManager_Reset = 4284;
const int M_CutterBaleManager_Load = 4285;
const int M_CutterBaleManager_Unload = 4286;
const int M_CutterBaleManager_Reposition = 4287;
const int M_CutterBaleManager_Reset = 4288;
const int M_CutterRightUsageCount_Reset = 4347;
const int M_CutterMiddleUsageCount_Reset = 4348;
const int M_CutterLeftUsageCount_Reset = 4349;
const int M_CutterUsageCount_Reset = 4350;
const int M_GrabBaleGateCamera_ManualCapture = 4351;
const int F_Grabbed01_toggle = 4298;
const int F_Grabbed02_toggle = 4299;
const int F_Grabbed03_toggle = 4300;
const int F_Grabbed04_toggle = 4301;
const int F_Grabbed05_toggle = 4302;
const int F_Grabbed06_toggle = 4303;
const int F_Grabbed07_toggle = 4304;
const int F_Grabbed08_toggle = 4305;
const int F_Grabbed09_toggle = 4306;
const int F_Grabbed10_toggle = 4307;
const int F_Grabbed11_toggle = 4308;
const int F_Grabbed12_toggle = 4309;
const int F_Grabbed13_toggle = 4310;
const int F_Grabbed14_toggle = 4311;
const int F_Grabbed15_toggle = 4312;
const int F_Grabbed16_toggle = 4313;
const int F_Grabbed17_toggle = 4314;
const int F_Grabbed18_toggle = 4315;
const int M_GrabBaleTodayCount_Reset = 4321;
const int M_CoreCycleStartLock_ClearLock = 4345;
const int Obj_PanelLot_CancelNext = 4268;
const int Obj_BaleFeed_ClearCycle = 4259;
const int Obj_BaleExitFeeder_ClearCycle = 4295;
const int Obj_BaleEnterCutter_ClearCycle = 4260;
const int Obj_BaleExitCutter_ClearCycle = 4261;
const int Obj_BaleEnterGrab_ClearCycle = 4262;
const int Obj_BaleExitGrab_ClearCycle = 4263;
const int M_GrabCycleStart_Start = 4257;
const int M_CutterChamber_Reposition = 4292;
const int M_GrabChamber_Reposition = 4293;
const int M_GrabSampleConveyor_toggle = 4317;
const int MM_CarriageSetup_Calibrate = 4290;
const int M_InsertFromReject_No = 4270;
const int M_InsertFromReject_Yes = 4271;
const int M_FeederResume_No = 4272;
const int M_FeederResume_Yes = 4273;
const int M_CutterResume_No = 4274;
const int M_CutterResume_Yes = 4275;
const int M_GrabResume_No = 4276;
const int M_GrabResume_Yes = 4277;
const int M_GrabControl2Panel_Reset = 4278;
const int P_CancelLotAndNextLot_ClearCycle = 4279;
const int M_GrabMotor_Start = 4155;
const int M_GrabMotor_Stop = 4156;
const int Obj_PanelLot_NextLotEntered = 4267;
const int Obj_BaleFeed_Cleared = 4328;
const int Obj_BaleExitFeeder_Cleared = 4329;
const int Obj_BaleEnterCutter_Cleared = 4330;
const int Obj_BaleExitCutter_Cleared = 4331;
const int Obj_BaleEnterGrab_Cleared = 4332;
const int Obj_BaleExitGrab_Cleared = 4333;
const int MM_CutterConveyorForward_Run = 4323;
const int MM_CutterConveyorReverse_Run = 4324;
const int MM_GrabConveyorForward_Run = 4325;
const int MM_GrabConveyorReverse_Run = 4326;


#endif