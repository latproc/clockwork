#ifndef __LOADER_CONFIG_H__
#define __LOADER_CONFIG_H__

class Fl_Widget;
class Fl_Value_Input;
extern void init(int, Fl_Widget*, bool is_input = true);
extern void init(int, Fl_Value_Input*);

const int I_BaleOnLoader = 2048;
const int I_BaleAtLoaderPos = 2049;
const int I_LoaderUp = 2050;
const int I_LoaderBlockOn = 2051;
const int I_PanelBlockIgnore = 2052;
const int I_X33BlockIgnore = 2053;

const int O_LoaderBlock = 2048;
const int M_WeightAvailable = 2049;

const int M_rawScales_rawWeight = 1000;
const int M_rawScales_rawDecWeight = 1001;
const int M_rawScales_rawSteady = 1002;
const int M_rawScales_rawUnderWeight = 1003;
const int M_rawScales_rawOverWeight = 1004;

#endif
