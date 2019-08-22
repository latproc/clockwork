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

#include "ExpressionAction.h"
#include "Logger.h"
#include "MachineInstance.h"

Action::Status ExpressionAction::run() {
	owner->start(this);
	status = Running;
	const Value &v = owner->getValue(lhs.get());
	if (v != SymbolTable::Null) {
		switch(op) {
		case ExpressionActionTemplate::opInc:
		case ExpressionActionTemplate::opDec:
			if (v.kind != Value::t_integer || rhs.kind != Value::t_integer)
				status = Failed;
			else
                owner->setValue(lhs.get(), v.iValue + rhs.iValue);
			break;
		case ExpressionActionTemplate::opSet:
			if (rhs.kind == Value::t_symbol) {
				const Value &rhs_v = owner->getValue(rhs.sValue);
				if (rhs_v == SymbolTable::Null) {
					owner->setValue(lhs.get(), rhs);
				}
				else {
					DBG_MSG << owner->getName() << " dereferenced property " << rhs << " and found " << rhs_v << "\n";
					owner->setValue(lhs.get(), rhs_v);
				}
			}
			else
				owner->setValue(lhs.get(), rhs);
			status = Complete;
			break;
		}
		if (status != Failed) {
			owner->setValue(lhs.get(), v);
			status = Complete;
		}
	}
	else {
		owner->setValue(lhs.get(), rhs);
		status = Complete;
	}
    owner->setNeedsCheck();
	owner->stop(this);
	return status;
}

Action::Status ExpressionAction::checkComplete() {
	return Complete;
}

std::ostream &ExpressionAction::operator<<(std::ostream &out) const {
    return out << "ExpressionAction " << lhs.get() << " " << op << " " << rhs << "\n";
}

Action *ExpressionActionTemplate::factory(MachineInstance *mi) { 
  return new ExpressionAction(mi, *this); 
}

ExpressionActionTemplate::ExpressionActionTemplate(CStringHolder var, opType oper) : lhs(var), rhs(1), op(oper) {
  if (op == opDec) rhs = -1;
}
ExpressionActionTemplate::ExpressionActionTemplate(CStringHolder var, opType oper, const Value &v) : lhs(var), rhs(v), op(oper) {
  if (op == opDec) rhs = -v;
}
ExpressionActionTemplate::ExpressionActionTemplate(CStringHolder var, opType oper, int a) : lhs(var), rhs(a), op(oper) {
  if (op == opDec) rhs = -a; else rhs = a;
}
ExpressionActionTemplate::ExpressionActionTemplate(CStringHolder var, opType oper, const char *a) : lhs(var), rhs(a), op(oper) {
  assert(op == opSet);
}
std::ostream &ExpressionActionTemplate::operator<<(std::ostream &out) const {
  switch(op) {
    case opInc: out << "INC "; break;
    case opDec: out << "DEC "; break;
    case opSet: out << "SET "; break;
  }
  return out << lhs.get() << " " << rhs << " " << "\n";
}

void ExpressionActionTemplate::toC(std::ostream &out, std::ostream &vars) const {
  out << "\t";
  operator<<(out);
  out << "\n";
}
