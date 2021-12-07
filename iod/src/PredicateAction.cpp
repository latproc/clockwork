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

#include "PredicateAction.h"
#include "Logger.h"
#include "DebugExtra.h"
#include "MachineInstance.h"
#include "regular_expressions.h"
#include "value.h"
#include "dynamic_value.h"

void breakpoint() {}

PredicateActionTemplate::~PredicateActionTemplate() {
    delete predicate;
}


Action *PredicateActionTemplate::factory(MachineInstance *mi) { 
  return new PredicateAction(mi, *this); 
}

Value resolve(Predicate *p, MachineInstance *m) {
	Value v = p->entry;
	p->clearError();
	if (v.kind == Value::t_symbol) {
		if (v.sValue == "TIMER") {
      v = *m->getTimerVal();
		}
		else {
			Value prop = m->getValue(v.sValue); // property lookup
			if (prop != SymbolTable::Null) {
				if (m && m->debug() ) {
					DBG_PREDICATES << "Using property " << p->entry 
						<< " to resolve search (" << prop << ", type: " << prop.kind << ")"
						<< ")\n";
				}
				return prop;
			}
			else if (m && m->debug()) {
				DBG_PREDICATES << "Predicate action resolve failed to resolve a value: " << v << "\n";
			}
		}
	}
    else if (v.kind == Value::t_dynamic) {
        DynamicValueBase *dv = v.dynamicValue();
        if (dv) return dv->operator()(m);
        return false;
    }
	return v;
}



Value eval(Predicate *p, MachineInstance *m){
	if (!p) return SymbolTable::Null;
	if (p->left_p || p->right_p) { 
		Value l;
		Value r;
		if (p->left_p) l = eval(p->left_p, m);
		if (p->right_p) r = eval(p->right_p, m);
		if (m && m->debug()){
			DBG_PREDICATES << " eval - left: "  << l<< " op: " << p->op << " right: " << r  << "\n";
		}
		Value res;
	    switch (p->op) {
			case opGE: res = l >= r; break;
			case opGT: res = l > r; break;
			case opLE: res = l <= r; break;
			case opLT: res = l < r; break;
			case opEQ: res = l == r; break;
			case opNE: res = l != r; break;
			case opAND: res = l && r; break;
			case opOR: res = l || r; break;
			case opNOT: res = !r; break;
			case opUnaryMinus: res = -r; break;
			case opPlus: res = l + r; break;
			case opMinus: res = l - r; break;
			case opTimes: res = l * r; break;
			case opDivide: res = l / r; break;
			case opAbsoluteValue: if (r < 0) res = -r; else res = r; break;
			case opMod: res = l % r; break;
            case opBitAnd: res = l & r; break;
            case opBitOr: res = l | r; break;
            case opBitXOr: res = l ^ r; break;
            case opNegate: res = ~r; break;
			case opInteger: res = r.trunc(); break;
			case opFloat: res = r.toFloat(); break;
			case opString: res = r.asString(); break;
			case opAssign: res = r; break; // TBD
            case opMatch: return matches(l.asString().c_str(), r.asString().c_str());
            case opAny:
            case opAll:
            case opIncludes:
            case opCount: {
                DynamicValueBase *dyn_v = r.dynamicValue();
                if (dyn_v) return dyn_v->operator()(m);
            }
                break;
	        case opNone: res = 0;
                break;
            default:
                std::cerr << "Error: unhandled operator " << p->op << " in evaluating predicate\n";
	    }
		
		if (m && m->debug()) {
			if (p->op == opNOT || p->op == opInteger || p->op == opFloat || p->op == opString) {
				DBG_PREDICATES << " expr: " << p->op << " " << *(p->right_p) << " returns " << res << "\n";
			}
			else {
				if (p->left_p && p->right_p) {
					DBG_PREDICATES << " expr: " << *(p->left_p) << " " << p->op << " " << *(p->right_p) << " returns " << res << "\n";
				}
				else if (p->left_p) {
					DBG_PREDICATES << " expr: " << *(p->left_p) << " " << p->op << " returns " << res << "\n";
				}
				else if (p->right_p) {
					DBG_PREDICATES << " expr: " << p->op << " " << *(p->right_p) << " returns " << res << "\n";
				}
			}
		}
		return res;
	}
	else
		return resolve(p, m);
}

PredicateAction::~PredicateAction() {
    delete predicate;
}

Action::Status PredicateAction::run() {
	owner->start(this);
	
	DBG_M_PREDICATES << " processing expression: " << *predicate << "\n";
	status = Running;
	if (predicate->left_p && predicate->left_p->entry.kind == Value::t_symbol) {
		std::string &name = predicate->left_p->entry.sValue;
		Value val;
		if (predicate->right_p)
			val = eval(predicate->right_p, owner);
		else
			val = owner->getValue(predicate->entry.sValue);
		if (owner->getStateMachine()->global_references.count(name)) {
			MachineInstance *global_machine = owner->getStateMachine()->global_references[name];
			if (!global_machine) {
				std::cerr << owner->getName() << " Error, can't find machine for global " 
					<< name << "\n" << std::flush;
				abort();
			}
			else {
				if (global_machine->_type != "CONSTANT") global_machine->setValue("VALUE", val);
			}
		}
		else {
            DBG_M_PREDICATES << "Telling " << owner->getName() << " to set property " << name << " to " 
							<< val << " (type: " << val.kind << ")\n";
			owner->setValue(name, val);
		}
	}
	else
	  assert(false);
	status = Complete;
	owner->stop(this);
	return status;
}

Action::Status PredicateAction::checkComplete() {
	return Complete;
}

std::ostream &PredicateAction::operator<<(std::ostream &out) const {
	out << "Assignment " << predicate->left_p->entry << " := " << *(predicate->right_p);
	return out;
}
