#ifndef __CORE_CONFIG_H__
#define __CORE_CONFIG_H__

class Fl_Widget;
class Fl_Value_Input;
extern void init(const char *, Fl_Widget*, bool is_input = true);
extern void init(const char *, Fl_Value_Input*);

extern const char *V_CoreKeepAlive;
extern const char *V_CoreScalesLiveWeight;
extern const char *V_CoreScalesCapturedWeight;
extern const char *F_CoreScalesSteady;
extern const char *I_CoreLoaderBalePresent;
extern const char *I_CoreLoaderAtPos;
extern const char *O_CoreLoaderUpBlock;
extern const char *O_CoreLoaderUp;

extern const char *I_GrabBalePresent;
extern const char *I_CutterBalePresent;
extern const char *I_FeederBalePresent;
extern const char *I_InsertBalePresent; 
extern const char *O_InsertForward;

extern const char *I_GrabBaleAtBaleGate;
extern const char *O_GrabBaleGateUp;
extern const char *O_GrabBaleGateBlock;
extern const char *V_GrabBaleGateFolioSize;
extern const char *V_GrabBaleGateBaleNo;


#endif
