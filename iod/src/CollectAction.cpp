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

#include "CollectAction.h"
#include "MachineInstance.h"
#include "Logger.h"
#include "DebugExtra.h"

Action *CollectActionTemplate::factory(MachineInstance *mi)
{
  return new CollectAction(mi, this);
}

std::ostream &CollectAction::operator<<(std::ostream &out) const
{
  MachineInstance *machine = owner->lookup(machine_name);

  out << "Collect Action " << machine_name;
  if (machine) {
    out << " Collect held by " << machine->Collecter()->getName();
  }
  out << "\n";
  return out;
}

Action::Status CollectAction::run()
{
  owner->start(this);
  machine = owner->lookup(machine_name);
  if (machine)
    if (machine->Collect(owner)) {
      status = Complete;
    }
    else {
      DBG_M_ACTIONS << owner->getName() << " waiting for Collect: " << *this;
      status = Running;
    }
  else {
    status = Failed;
  }

  if (status == Complete || status == Failed) {
    owner->stop(this);
  }
  return status;
}

Action::Status CollectAction::checkComplete()
{
  if (status == Complete || status == Failed) {
    return status;
  }
  if (this != owner->executingCommand()) {
    DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
  }
  else {
    if (machine->Collect(owner))  {
      status = Complete;
      owner->stop(this);
    }
    else {
      status = Running;
    }
  }
  return status;
}

