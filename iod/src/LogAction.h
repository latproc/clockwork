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

#ifndef __LOGACTION_H__
#define __LOGACTION_H__ value

#include <iostream>
#include "Action.h"
#include "symboltable.h"
#include "Expression.h"

class MachineInstance;

struct LogActionTemplate : public ActionTemplate {
    LogActionTemplate(CStringHolder msg) : message(msg.get()), predicate(0) { }
    LogActionTemplate(const Value &v) : message(v), predicate(0) { }
    LogActionTemplate(Predicate *pred) : message(SymbolTable::Null), predicate(pred) { }
    virtual Action *factory(MachineInstance *mi) override;
	  virtual std::ostream &operator<<(std::ostream &out) const override;
    virtual void toC(std::ostream &out) const override;
    Value message;
    Predicate *predicate;
};

struct LogAction : public Action {
    LogAction(MachineInstance *mi, LogActionTemplate &lat)
    : Action(mi), message(lat.message), predicate(lat.predicate) { }
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
    Value message;
    Predicate *predicate;
};

#endif
