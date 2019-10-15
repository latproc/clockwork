# ANALOGINPUT is a new machine class and its usage is changing.

ANALOGIN_SETTINGS MACHINE { 
# a third-order pass-through filter that doesn't cutoff any frequencies
#  C LIST 1.0,3.0,3.0,1.0;
#  D LIST 1.0,3.0,3.0,1.0;

#C LIST 0.000061006178758, 0.000122012357516, 0.000061006178758;
#D LIST 1.000000000000, -1.977786483777, 0.978030508492;

# a third order low pass filter that cuts of at 0.01*Pi
  C LIST 0.000003756838020,0.000011270514059,0.000011270514059,0.000003756838020;
  D LIST 1.000000000000,-2.937170728450,2.876299723479,-0.939098940325;

  idle INITIAL;
}
ain_settings ANALOGIN_SETTINGS(filter:2);
ain_underrange POINT(export:ro) Beckhoff_EL3164, 0;
ain_overrange POINT(export:ro) Beckhoff_EL3164, 1;
ain_error POINT(export:ro) Beckhoff_EL3164, 4;
ain ANALOGINPUT Beckhoff_EL3164, 10, ain_settings;

#Status__Underrange
#Status__Overrange
#Status__Limit1
#Status__Limit2
#Status__Error
#Status__Sync error
#Status__TxPDO State
#Status__TxPDO Toggle
#AI Standard Channel 1 Value



