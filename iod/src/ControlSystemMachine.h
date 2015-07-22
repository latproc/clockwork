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

#include <iostream>
#include "ECInterface.h"
#include "IOComponent.h"

class ControlSystemMachine : IOComponent {
public:
	ControlSystemMachine();

    /* states */
    bool ready() const { return state == s_operational; }
    bool connected()  const { return state == s_connected || state == s_operational; }

#ifdef EC_SIMULATOR
    /* conditions */
    bool c_connected() const { return true; }
    bool c_disconnected() const { return false; }
    bool c_online() const { return true; }
    bool c_operational() const { return true; }
#else
    /* conditions */
    bool c_connected() const { return ECInterface::domain1_state.wc_state == 2 && ECInterface::master_state.link_up; }
    bool c_disconnected() const { return ECInterface::domain1_state.wc_state != 2 || !ECInterface::master_state.link_up; }
    bool c_online() const { return !c_disconnected() && ECInterface::instance()->online(); }
    bool c_operational() const { return !c_disconnected() && ECInterface::instance()->operational(); }
#endif

    void idle() {
        switch(state) {
            case s_unknown:
                if (c_connected()) enter_connected();
            case s_connected:
                if (c_online()) enter_slaves_online();
            case s_slaves_online:
                if (c_operational()) enter_operational();
                break;
            case s_operational:
                if (c_disconnected()) enter_disconnected();

                break;
            case s_disconnected:
                if (!c_disconnected()) enter_unknown();
        }
    }

protected:
    enum states {s_unknown, s_disconnected, s_connected, s_slaves_online, s_operational};
    states state;

    enum events { e_start, e_none };
    events last_event;

    void enter_connected() {
        state = s_connected;
		std::cout << "Control System is connected\n "; }
    void enter_disconnected() {
        state = s_disconnected;
        std::cout << "Control System is disconnected\n ";
    }
    void enter_slaves_online() {
        state = s_slaves_online;
        std::cout << "Control System slaves are online\n ";
    }
    void enter_operational() {
        state = s_operational;
        std::cout << "Control System is operational\n ";
    }
    void enter_unknown() {
        state=s_unknown;
        std::cout << "\nControl System is in an unknown state\n ";
    }

};
