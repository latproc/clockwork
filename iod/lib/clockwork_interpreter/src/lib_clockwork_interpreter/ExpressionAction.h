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

#ifndef __EXPRESSIONACTION_H__
#define __EXPRESSIONACTION_H__ 1

#include <iostream>
#include <lib_clockwork_client/includes.hpp>
#include "Expression.h"
#include "Action.h"
// // #include "symboltable.h"

struct ExpressionActionTemplate : public ActionTemplate {
	enum opType { opInc, opDec, opSet };
  ExpressionActionTemplate(CStringHolder var, opType oper);
  ExpressionActionTemplate(CStringHolder var, opType oper, const Value &v);
  ExpressionActionTemplate(CStringHolder var, opType oper, int a);
  ExpressionActionTemplate(CStringHolder var, opType oper, const char *a);
  virtual Action *factory(MachineInstance *mi);
  std::ostream &operator<<(std::ostream &out) const;
  void toC(std::ostream &out, std::ostream &vars) const;

	CStringHolder lhs;
	Value rhs;
	opType op;
};

struct ExpressionAction : public Action {
	ExpressionAction(MachineInstance *mi, ExpressionActionTemplate &eat) : Action(mi), lhs(eat.lhs), rhs(eat.rhs), op(eat.op) { }
	Status run();
	Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out)const;
	CStringHolder lhs;
	Value rhs;
	ExpressionActionTemplate::opType op;
	MachineInstance *machine;
};

#endif
