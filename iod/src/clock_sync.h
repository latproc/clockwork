#pragma once

void init_clock_sync();

// block until the next clock tick
void clock_sync();

void clock_stop();
