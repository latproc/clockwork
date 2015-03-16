#ifndef __LOADER_CONFIG_H__
#define __LOADER_CONFIG_H__

class Fl_Widget;
extern void init(int, Fl_Widget*, bool is_input = true);

const int I_BaleOnLoader = 2048;
const int I_BaleAtLoaderPos = 2049;
const int I_LoaderUp = 2050;
const int I_LoaderBlockOn = 2051;
const int I_PanelBlockIgnore = 2052;
const int I_X33BlockIgnore = 2053;

const int O_LoaderBlock = 2048;
const int M_WeightAvailable = 2049;

#endif
