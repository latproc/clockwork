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

#ifndef __PREDICATEACTION_H_
#define __PREDICATEACTION_H_ 1

#include <iostream>
#include "Expression.h"
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

struct PredicateActionTemplate : public ActionTemplate {
  PredicateActionTemplate(Predicate *p) : predicate(p) { }
  ~PredicateActionTemplate();
  virtual Action *factory(MachineInstance *mi);
  std::ostream &operator<<(std::ostream &out) const;
  void toC(std::ostream &out) const;
  Predicate *predicate;
};

struct PredicateAction : public Action {
	PredicateAction(MachineInstance *mi, PredicateActionTemplate &pat) : Action(mi), predicate(new Predicate(*pat.predicate)) { }
    ~PredicateAction();
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
	Predicate *predicate;
};

#endif
