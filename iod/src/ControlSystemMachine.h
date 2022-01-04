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

#include "ECInterface.h"
#include "IOComponent.h"
#include <iostream>

class ControlSystemMachine : IOComponent {
  public:
    ControlSystemMachine();

    /* states */
    bool ready() const;
    bool connected() const;

    /* conditions */
    bool c_connected() const;    // ethercat link is up
    bool c_disconnected() const; // ethercat link is down/unknown
    bool c_active() const;       // ethercat master is active
    bool c_online() const;       // all ethercat slaves are online
    bool c_operational() const;  // all ethercat slaves are operational

    void idle();
    void requestActivation(bool which);
    bool activationRequested();
    void requestDeactivation(bool which);
    bool deactivationRequested();
    uint64_t lastUpdated();

  protected:
    enum states { s_unknown, s_disconnected, s_connected, s_slaves_online, s_operational };
    states state;

    enum events { e_start, e_none };
    events last_event;

    void enter_connected();
    void enter_disconnected();
    void enter_slaves_online();
    void enter_operational();
    void enter_unknown();

    void sync_slaves();

    MachineInstance *ethercat_machine;

    bool activate_requested;
    bool deactivate_requested;
    uint64_t ecat_last_synced;    // time that ethercat status was last synced to clockwork
    unsigned int ecat_last_state; // time that ethercat status was last synced to clockwork
};
