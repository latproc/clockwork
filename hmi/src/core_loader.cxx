// generated by Fast Light User Interface Designer (fluid) version 1.0303

#include "core_loader.h"
#include "core_config.h"

Fl_Double_Window* CoreLoader::make_window() {
  Fl_Double_Window* w;
  { Fl_Double_Window* o = new Fl_Double_Window(903, 489);
    w = o;
    o->user_data((void*)(this));
    { Fl_Light_Button* o = new Fl_Light_Button(40, 305, 205, 40, "I_CoreLoaderAtPos");
      o->callback((Fl_Callback*)press, (void*)(&I_CoreLoaderAtPos));
      o->align(Fl_Align(324|FL_ALIGN_INSIDE));
      init(I_CoreLoaderAtPos, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(40, 345, 205, 40, "O_CoreLoaderUpBlock");
      o->callback((Fl_Callback*)press, (void*)(&O_CoreLoaderUpBlock));
      init(O_CoreLoaderUpBlock, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(40, 385, 205, 40, "O_CoreLoaderUp");
      o->callback((Fl_Callback*)press, (void*)(&O_CoreLoaderUp));
      init(O_CoreLoaderUp, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(310, 304, 205, 40, "I_FeederBalePresent");
      o->callback((Fl_Callback*)press, (void*)(&I_FeederBalePresent));
      init(I_FeederBalePresent, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(310, 344, 205, 40, "I_InsertBalePresent");
      o->callback((Fl_Callback*)press, (void*)(&I_InsertBalePresent));
      init(I_InsertBalePresent, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(310, 385, 205, 40, "O_InsertForward");
      o->callback((Fl_Callback*)press, (void*)(&O_InsertForward));
      init(O_InsertForward, o);
    } // Fl_Light_Button* o
    { Fl_Value_Input* o = new Fl_Value_Input(230, 50, 115, 35, "V_CoreKeepAlive");
      o->callback((Fl_Callback*)save, (void*)(&V_CoreKeepAlive));
      init(V_CoreKeepAlive, o);
    } // Fl_Value_Input* o
    { Fl_Value_Input* o = new Fl_Value_Input(230, 100, 115, 35, "V_CoreScalesLiveWeight");
      o->callback((Fl_Callback*)save, (void*)(&V_CoreScalesLiveWeight));
      init(V_CoreScalesLiveWeight, o);
    } // Fl_Value_Input* o
    { Fl_Value_Input* o = new Fl_Value_Input(230, 150, 115, 40, "V_CoreScalesCapturedWeight");
      o->callback((Fl_Callback*)save, (void*)(&V_CoreScalesCapturedWeight));
      init(V_CoreScalesCapturedWeight, o);
    } // Fl_Value_Input* o
    { Fl_Value_Input* o = new Fl_Value_Input(650, 50, 115, 35, "V_GrabBaleGateFolioSize");
      o->callback((Fl_Callback*)save, (void*)(&V_GrabBaleGateFolioSize));
      init(V_GrabBaleGateFolioSize, o);
    } // Fl_Value_Input* o
    { Fl_Value_Input* o = new Fl_Value_Input(650, 100, 115, 40, "V_GrabBaleGateBaleNo");
      o->callback((Fl_Callback*)save, (void*)(&V_GrabBaleGateBaleNo));
      init(V_GrabBaleGateBaleNo, o);
    } // Fl_Value_Input* o
    { Fl_Light_Button* o = new Fl_Light_Button(40, 225, 205, 40, "F_CoreScalesSteady");
      o->callback((Fl_Callback*)press, (void*)(&F_CoreScalesSteady));
      o->align(Fl_Align(324|FL_ALIGN_INSIDE));
      init(F_CoreScalesSteady, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(40, 265, 205, 40, "I_CoreLoaderBalePresent");
      o->callback((Fl_Callback*)press, (void*)(&I_CoreLoaderBalePresent));
      init(I_CoreLoaderBalePresent, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(310, 220, 205, 40, "I_GrabBalePresent");
      o->callback((Fl_Callback*)press, (void*)(&I_GrabBalePresent));
      init(I_GrabBalePresent, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(310, 260, 205, 40, "I_CutterBalePresent");
      o->callback((Fl_Callback*)press, (void*)(&I_CutterBalePresent));
      init(I_CutterBalePresent, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(580, 305, 205, 40, "O_GrabBaleGateBlock");
      o->callback((Fl_Callback*)press, (void*)(&O_GrabBaleGateBlock));
      init(O_GrabBaleGateBlock, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(580, 221, 205, 40, "I_GrabBaleAtBaleGate");
      o->callback((Fl_Callback*)press, (void*)(&I_GrabBaleAtBaleGate));
      init(I_GrabBaleAtBaleGate, o);
    } // Fl_Light_Button* o
    { Fl_Light_Button* o = new Fl_Light_Button(580, 261, 205, 40, "O_GrabBaleGateUp");
      o->callback((Fl_Callback*)press, (void*)(&O_GrabBaleGateUp));
      init(O_GrabBaleGateUp, o);
    } // Fl_Light_Button* o
    o->end();
  } // Fl_Double_Window* o
  return w;
}
