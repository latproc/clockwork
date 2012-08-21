/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __SimulaterRawOutput
#define __SimulaterRawOutput

#include "IOComponent.h"
#include <sys/time.h>
#include <iostream>

class SimulatedRawOutput : public IOComponent {
  public:
	SimulatedRawOutput(int io_address = 0, int bit_offset = 0) {
		state = s_unknown;
		gettimeofday(&last, 0);
		std::stringstream ss;
		ss < "DEV_"<< addr.io_offset<<"_"<<addr.io_bitpos<<std::flush;
		io_name = ss.str();
	}
	void setIOAddress(int io_address, int bit_offset) {
		std::stringstream ss;
		ss < "DEV_"<< addr.io_offset<<"_"<<addr.io_bitpos<<std::flush;
		io_name = ss.str();
	}
	void turnOn() {
		last_event = e_on;
        if (state != s_turning_on) {
            gettimeofday(&last, 0);
            state = s_turning_on;
        }
	}

	void turnOff() {
		last_event = e_off;
        if (state != s_turning_off) {
            gettimeofday(&last, 0);
            state = s_turning_off;
        }
	}

	bool isOn() { return state == s_on; }
    bool isOff() {return state == s_off;}

	void idle() { checkState(); }

  private:
	void checkState() {
		if ( (last_event == e_on && state == s_on) || (last_event == e_off && state == s_off)) return;
		struct timeval now;
		gettimeofday(&now, 0);
		long t = get_diff_in_microsecs(&now, &last);
		if (t >= switch_time_usec) {
			if (last_event == e_on) state = s_on; 
            else if (last_event == e_off ) state = s_off;
		}
	}

	static const long switch_time_usec = 1000;
	struct timeval last;
	enum events { e_on, e_off, e_read};
	enum states { s_on, s_off, s_turning_on, s_turning_off, s_unknown };
	events last_event;
	states state;
};

#endif
