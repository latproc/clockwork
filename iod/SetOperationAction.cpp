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

#include "SetOperationAction.h"
#include "MachineInstance.h"
#include "Logger.h"

SetOperationActionTemplate::SetOperationActionTemplate(Value num, Value a, Value b,
                                                       Value destination, Value property, SetOperation op,
                                                       Predicate *pred, bool remove, Value start, Value end)
    :   count(num), src_a_name(a.asString()), src_b_name(b.asString()),
        dest_name(destination.asString()), property_name(property.asString()),
        operation(op),
        condition(pred),
        remove_selected(remove), start_pos(start), end_pos(end) {
}

SetOperationActionTemplate::~SetOperationActionTemplate() {
}
                                           
Action *SetOperationActionTemplate::factory(MachineInstance *mi) {
    switch (operation) {
        case soIntersect: return new IntersectSetOperation(mi, this);
        case soUnion: return new UnionSetOperation(mi, this);
        case soDifference:return new DifferenceSetOperation(mi, this);
        case soSelect: return new SelectSetOperation(mi, this);
    }
    return 0;
}

SetOperationAction::SetOperationAction(MachineInstance *m, const SetOperationActionTemplate *dat)
    : scope(m), count(dat->count), Action(m),
        source_a(dat->src_a_name), source_b(dat->src_b_name),
        source_a_machine(0), source_b_machine(0),
        dest(dat->dest_name), dest_machine(0),  operation(dat->operation),
        property_name(dat->property_name), condition(dat->condition), remove_selected(dat->remove_selected) {
}

SetOperationAction::SetOperationAction() : dest_machine(0) {
}

std::ostream &SetOperationAction::operator<<(std::ostream &out) const {
	return out << "Set Operation " << source_a << ", " << source_b << " to " << dest << "\n";
}

class SetOperationException : public std::exception {
    
};

bool CompareValues(Value a, Value &b){
    if (a == SymbolTable::Null || b == SymbolTable::Null) throw SetOperationException();
    return a == b;
}

bool CompareSymbolAndValue(MachineInstance*scope, Value &sym, std::string &prop, Value &b){
    MachineInstance *mi = scope->lookup(sym);
    if (!mi) throw new SetOperationException();
    return CompareValues(mi->getValue(prop), b);
}

bool MachineIncludesParameter(MachineInstance *m, Value &param) {
    for (int i=0; i<m->parameters.size(); ++i) {
        if (m->parameters[i].val == param) return true;
    }
    return false;
}

Action::Status SetOperationAction::run() {
	owner->start(this);
    try {
        if (!source_a_machine) source_a_machine = owner->lookup(source_a);
        if (!source_b_machine) source_b_machine = owner->lookup(source_b);
        if (!dest_machine) dest_machine = owner->lookup(dest);
        if (dest_machine && dest_machine->_type == "LIST") {
            status = doOperation();
        }
        else
            status = Failed;
    }
    catch (SetOperationException &e) {
        status = Failed;
    }
    owner->stop(this);
	return status;
}

Action::Status SetOperationAction::doOperation() {
    return Failed;
}

Action::Status SetOperationAction::checkComplete() {
	if (status == Complete || status == Failed) return status;
	if (this != owner->executingCommand()) {
		DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
	}
	else {
		status = Complete;
		owner->stop(this);
	}
	return status;
}

IntersectSetOperation::IntersectSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat)
: SetOperationAction(m, dat)
{
/*
    if (start_pos.kind == Value::t_symbol || start_pos.kind == Value::t_string) {
        Value v = m->properties.lookup(start_pos.sValue.c_str());
        if (!v.asInteger(sp)) sp = 0;
    }
    else {
        if (!start_pos.asInteger(sp)) sp = 0;
    }
    if (sp == -1) sp = 0;
    if (ep == -1) sp = 0;
    if (end_pos.kind == Value::t_symbol || end_pos.kind == Value::t_string) {
        Value v = m->properties.lookup(start_pos.sValue.c_str());
        if (!v.asInteger(ep)) ep = -1;
    }
    else {
        if (!end_pos.asInteger(ep)) ep = -1;
    }
    if (ep == -1) count = -1; else count = ep - sp + 1;
*/
}

void setListItem(MachineInstance *list, const Value &item) {
    list->removeLocal(0);
    list->addLocal(item, item.cached_machine);
    list->locals[0].val = Value("ITEM");
    list->locals[0].real_name = item.sValue;
    list->locals[0].machine = item.cached_machine;
    if (list->locals[0].machine)
        list->locals[0].machine->getCurrentValue()->setDynamicValue(new MachineValue(list->locals[0].machine, item.sValue));
}

Action::Status IntersectSetOperation::doOperation() {
    unsigned int num_copied = 0;
    long to_copy;
    if (!source_a_machine) {
        status = Failed;
        return status;
    }
    if (count == -1 || !count.asInteger(to_copy)) to_copy = source_a_machine->parameters.size();
    unsigned int i=0;
    while (i < source_a_machine->parameters.size()) {
        Value &a(source_a_machine->parameters.at(i).val);
        MachineInstance *mi = owner->lookup(a);
        if (!mi) { ++i; continue; }
        assert(a.cached_machine);
        setListItem(source_a_machine, a);
        for (unsigned int j = 0; j < source_b_machine->parameters.size(); ++j) {
            Value &b(source_b_machine->parameters.at(j).val);
            if (!b.cached_machine) owner->lookup(b);
            if (!b.cached_machine) { continue; }
            Value v1(a);
            setListItem(source_b_machine, b);
            if (condition.predicate) {
                bool matched = false;
                condition.last_result = SymbolTable::Null;
                condition.predicate->flushCache();
                condition.predicate->stack.clear();
                //std::cout << "testing: " << a << " and " << b << "\n";
                if (condition(owner)) {
                    if (!MachineIncludesParameter(dest_machine,a)) {
                        dest_machine->addParameter(a, v1.cached_machine);
                        ++num_copied;
                    }
                    matched = true;
                    if (remove_selected) {
                        source_a_machine->removeLocal(0);
                        source_a_machine->removeParameter(i);
/*
                        mi->stopListening(source_a_mchine);
                        mi->removeDependancy(source_a_machine);
                        source_a_machine->removeDependancy(mi);
                        source_a_machine->stopListening(mi);
                        source_a_machine->parameters.erase(source_a_machine->parameters.begin()+i);
                        source_a_machine->setNeedsCheck();
 */
                    }
                }
                if (num_copied >= to_copy) goto doneIntersectOperation;
                if (matched && remove_selected) goto intersectSkipToNextSourceItem; // skip the increment to next parameter
                if (matched) break; // already matched this item, no need to keep looking
            }
            else {
                if (v1.kind == Value::t_symbol && (b.kind == Value::t_string || b.kind == Value::t_integer)) {
                    if (CompareSymbolAndValue(owner, v1, property_name, b)) {
                        dest_machine->addParameter(a, v1.cached_machine);
                        break;
                    }
                }
                else if (b.kind == Value::t_symbol && (v1.kind == Value::t_string || v1.kind == Value::t_integer)) {
                    if (CompareSymbolAndValue(owner, b, property_name, v1)) {
                        dest_machine->addParameter(a, v1.cached_machine);
                        break;
                    }
                }
                else if (a == b) dest_machine->addParameter(a);
            }
        }
        ++i;
    intersectSkipToNextSourceItem: ;
    }
doneIntersectOperation:
    source_b_machine->removeLocal(0);
    source_b_machine->removeLocal(0);
    std::stringstream ss;
    const char *delim="";
    ss << "[";
    for (unsigned int i=0; i<dest_machine->parameters.size(); ++i) {
        ss << delim << dest_machine->parameters[i].val;
        delim = ",";
    }
    ss << "]";
    dest_machine->setValue("DEBUG", ss.str().c_str());
    status = Complete;
    return status;
}

UnionSetOperation::UnionSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat)
: SetOperationAction(m, dat)
{
    
}

Action::Status UnionSetOperation::doOperation() {
    if (source_a_machine != dest_machine)
        for (unsigned int i=0; i < source_a_machine->parameters.size(); ++i) {
            Value &a(source_a_machine->parameters.at(i).val);
            if (a.kind == Value::t_symbol) {
                MachineInstance *mi = owner->lookup(a);
                if (!mi) throw new SetOperationException();
                Value val(mi->getValue(a.sValue));
                if (!MachineIncludesParameter(dest_machine,val)) dest_machine->addParameter(a);
            }
            else {
                if (!MachineIncludesParameter(dest_machine,a)) dest_machine->addParameter(a);
            }
        }
    if (source_b_machine != dest_machine)
        for (unsigned int i=0; i < source_b_machine->parameters.size(); ++i) {
            Value &a(source_b_machine->parameters.at(i).val);
            if (a.kind == Value::t_symbol) {
                MachineInstance *mi = owner->lookup(a);
                if (!mi) throw new SetOperationException();
                Value val(mi->getValue(a.sValue));
                if (!MachineIncludesParameter(dest_machine,val)) dest_machine->addParameter(a);
            }
            else {
                if (!MachineIncludesParameter(dest_machine,a)) dest_machine->addParameter(a);
            }
        }
    std::stringstream ss;
    const char *delim="";
    ss << "[";
    for (unsigned int i=0; i<dest_machine->parameters.size(); ++i) {
        ss << delim << dest_machine->parameters[i].val;
        delim = ",";
    }
    ss << "]";
    dest_machine->setValue("DEBUG", ss.str().c_str());
    
    status = Complete;
    return status;
}

DifferenceSetOperation::DifferenceSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat)
: SetOperationAction(m, dat)
{
    
}

Action::Status DifferenceSetOperation::doOperation() {
    for (unsigned int i=0; i < source_a_machine->parameters.size(); ++i) {
        Value &a(source_a_machine->parameters.at(i).val);
        bool found = false;
        for (unsigned int j = 0; j < source_b_machine->parameters.size(); ++j) {
            Value &b(source_b_machine->parameters.at(j).val);
            Value v1(a);
            if (v1.kind == Value::t_symbol && (b.kind == Value::t_string || b.kind == Value::t_integer)) {
                if (CompareSymbolAndValue(owner, v1, property_name, b)) {
                    found = true; break;
                }
            }
            else if (b.kind == Value::t_symbol && (v1.kind == Value::t_string || v1.kind == Value::t_integer)) {
                if (CompareSymbolAndValue(owner, b, property_name, v1)) {
                    found = true; break;
                }
            }
            else if (a == b) { found = true; break; }
        }
        if (!found) dest_machine->addParameter(a);
    }
    std::stringstream ss;
    const char *delim="";
    ss << "[";
    for (unsigned int i=0; i<dest_machine->parameters.size(); ++i) {
        ss << delim << dest_machine->parameters[i].val;
        delim = ",";
    }
    ss << "]";
    dest_machine->setValue("DEBUG", ss.str().c_str());
    status = Complete;
    return status;
}


SelectSetOperation::SelectSetOperation(MachineInstance *m, const SetOperationActionTemplate *dat)
: SetOperationAction(m, dat)
{
    /*
    if (start_pos.kind == Value::t_symbol || start_pos.kind == Value::t_string) {
        Value v = m->properties.lookup(start_pos.sValue.c_str());
        if (!v.asInteger(sp)) sp = 0;
    }
    else {
        if (!start_pos.asInteger(sp)) sp = 0;
    }
    if (sp == -1) sp = 0;
    if (ep == -1) sp = 0;
    if (end_pos.kind == Value::t_symbol || end_pos.kind == Value::t_string) {
        Value v = m->properties.lookup(start_pos.sValue.c_str());
        if (!v.asInteger(ep)) ep = -1;
    }
    else {
        if (!end_pos.asInteger(ep)) ep = -1;
    }
    if (ep == -1) count = -1; else count = ep - sp + 1;
     */
}

Action::Status SelectSetOperation::doOperation() {
    int num_copied = 0;
    long to_copy;
    if (source_a_machine) {
        if (count < 0 || !count.asInteger(to_copy)) to_copy = source_a_machine->parameters.size();
        unsigned int i=0;
        while (i < source_a_machine->parameters.size()) {
            //if (i<sp) continue;
            //if (i>ep) break;
            Value &a(source_a_machine->parameters.at(i).val);
            if (a.kind == Value::t_symbol) {
                MachineInstance *mi = owner->lookup(a);
                source_a_machine->removeLocal(0);
                source_a_machine->addLocal(a, mi);
                source_a_machine->locals[0].val = Value("ITEM");
                source_a_machine->locals[0].real_name = a.sValue;
                if (!mi) throw new SetOperationException();
                if (condition.predicate) condition.predicate->flushCache();
                if ( (!condition.predicate || condition(owner)) ){
                    if (!MachineIncludesParameter(dest_machine,a)) {
                        dest_machine->addParameter(a);
                        ++num_copied;
                    }
                    if (remove_selected) {
                        source_a_machine->removeLocal(0);
                        source_a_machine->removeParameter(i);
                        /*
                        mi->stopListening(source_a_machine);
                        mi->removeDependancy(source_a_machine);
                        source_a_machine->removeDependancy(mi);
                        source_a_machine->stopListening(mi);
                        source_a_machine->parameters.erase(source_a_machine->parameters.begin()+i);
                        source_a_machine->setNeedsCheck();
                         */
                    }
                    if (num_copied >= to_copy) break;
                    if (remove_selected) continue; // skip the increment to next parameter
                }
            }
            else {
                if (!MachineIncludesParameter(dest_machine,a)) dest_machine->addParameter(a);
            }
            ++i;
        }
        source_a_machine->removeLocal(0);
    }
    std::stringstream ss;
    const char *delim="";
    ss << "[";
    for (unsigned int i=0; i<dest_machine->parameters.size(); ++i) {
        ss << delim << dest_machine->parameters[i].val;
        delim = ",";
    }
    ss << "]";
    dest_machine->setValue("DEBUG", ss.str().c_str());
    status = Complete;
    return status;
}


