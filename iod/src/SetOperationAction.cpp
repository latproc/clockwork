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


static void debugParameterChange(MachineInstance *dest_machine) {
	const char *delim="";
	char buf[1000];
	snprintf(buf, 1000, "[");
	size_t n = 1;
	for (unsigned int i=0; i<dest_machine->parameters.size(); ++i) {
		snprintf(buf+n,1000-n,"%s%s",delim,dest_machine->parameters[i].val.asString().c_str());
		n += strlen(delim) + dest_machine->parameters[i].val.asString().length();
		delim = ",";
	}
	snprintf(buf+n, 1000-n, "]");
	dest_machine->setValue("DEBUG", buf);
}

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
    : Action(m), scope(m), count(dat->count), 
        source_a(dat->src_a_name), source_b(dat->src_b_name), dest(dat->dest_name),
        condition(dat->condition), property_name(dat->property_name),
				source_a_machine(0), source_b_machine(0),
        dest_machine(0),  operation(dat->operation),
			 remove_selected(dat->remove_selected), start_pos(dat->start_pos), end_pos(dat->end_pos) {
}

SetOperationAction::SetOperationAction() : Action(), dest_machine(0) {
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
    if (!mi) throw
        new SetOperationException();
    return CompareValues(mi->getValue(prop), b);
}

bool MachineIncludesParameter(MachineInstance *m, Value &param) {
    for (unsigned int i=0; i<m->parameters.size(); ++i) {
        if (m->parameters[i].val == param) return true;
    }
    return false;
}

Action::Status SetOperationAction::run() {
	owner->start(this);
    try {
		// do not retain cached values since set operations may be working
		// with changed items from other lists
		source_a.cached_machine = 0;
		source_b.cached_machine = 0;
		dest.cached_machine = 0;
        source_a_machine = owner->lookup(source_a);
        source_b_machine = owner->lookup(source_b);
        dest_machine = owner->lookup(dest);
        if (dest_machine && (dest_machine->_type == "LIST" || dest_machine->_type == "REFERENCE") ) {
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

void setListItem(MachineInstance *list, const Value &item, int index, bool &add_item) {
	if (add_item) {
		list->locals.push_back(item);
		add_item = false;
	}
    list->locals[index].val = Value("ITEM");
    list->locals[index].real_name = item.sValue;
    list->locals[index].machine = item.cached_machine;
	if (item.cached_machine)
		list->localised_names["ITEM"] = item.cached_machine;
}

class IndexTracker {
public:
	bool add_item;
	bool keep_item;
	unsigned int index;
	IndexTracker(std::vector<Parameter>&locals) {
		// find or create the index to be used for the ITEM reference
		keep_item = false;
		add_item = true;
		index = 0;
		while (index < locals.size()) {
			if (locals[index].val.asString() == "ITEM") {
				keep_item = true;
				add_item = false;
				break;
			}
			++index;
		}
	}
};

Action::Status IntersectSetOperation::doOperation() {
    unsigned int num_copied = 0;
    long to_copy;
    if (!source_a_machine) {
        status = Failed;
        return status;
    }
    if (count == -1 || !count.asInteger(to_copy)) 
			to_copy = source_a_machine->parameters.size();
#ifdef DEPENDENCYFIX
    MachineInstance *last_machine_a = 0;
    MachineInstance *last_machine_b = 0;
    Value last_a;
    Value last_b;
#endif
	IndexTracker track_a(source_a_machine->locals);
	IndexTracker track_b(source_b_machine->locals);

	unsigned int i=0;
    while (i < source_a_machine->parameters.size()) {
        Value &a(source_a_machine->parameters.at(i).val);
        MachineInstance *mi = owner->lookup(a);
        if (!mi) { ++i; continue; }
        assert(a.cached_machine);
        setListItem(source_a_machine, a, track_a.index, track_a.add_item);
#ifdef DEPENDENCYFIX
        if (last_machine_a && !remove_selected) {
            if (MachineIncludesParameter(source_a_machine,last_a)) {
                source_a_machine->addDependancy(last_machine_a);
                last_machine_a->listenTo(source_a_machine);
                last_machine_a->addDependancy(source_a_machine);
            }
        }
        last_a = a;
        last_machine_a = mi;
        
#endif
        for (unsigned int j = 0; j < source_b_machine->parameters.size(); ++j) {
            Value &b(source_b_machine->parameters.at(j).val);
            if (!b.cached_machine) owner->lookup(b);
            if (!b.cached_machine) { continue; }
            Value v1(a);
            setListItem(source_b_machine, b, track_b.index, track_b.add_item);
#ifdef DEPENDENCYFIX
            if (last_machine_b && !remove_selected) {
                if (MachineIncludesParameter(source_b_machine,last_b)) {
                    source_b_machine->addDependancy(last_machine_b);
                    last_machine_b->listenTo(source_b_machine);
                    last_machine_b->addDependancy(source_b_machine);
                }
            }
            last_b = b;
            last_machine_b = b.cached_machine;
#endif
			source_a_machine->localised_names.erase("ITEM");
			source_b_machine->localised_names.erase("ITEM");
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
                        source_a_machine->removeParameter(i);
                    }
                }
                if (num_copied >= to_copy) goto doneIntersectOperation;
                if (matched && remove_selected) goto intersectSkipToNextSourceItem; // skip the increment to next parameter
                if (matched) break; // already matched this item, no need to keep looking
            }
            else {
                if (v1.kind == Value::t_symbol 
					&& (b.kind == Value::t_string || b.kind == Value::t_integer)) {
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
				else if (property_name.length()) {
					const Value &v1 = a.cached_machine->getValue(property_name);
					const Value &v2 = b.cached_machine->getValue(property_name);
					if (v1 != SymbolTable::Null && v1 == v2) {
						dest_machine->addParameter(a, a.cached_machine);
					}
				}
                else if (a == b) dest_machine->addParameter(a);
            }
        }
        ++i;
    intersectSkipToNextSourceItem: ;
    }
doneIntersectOperation:
	if (!track_a.add_item && !track_a.keep_item && source_a_machine->locals.size()) {
		std::vector<Parameter>::iterator iter = source_a_machine->locals.begin();
		for (unsigned int i=0; i<track_a.index; ++i, iter++) {;}
		source_a_machine->locals.erase(iter);
	}
	if (!track_b.add_item && !track_b.keep_item && source_b_machine->locals.size()) {
		std::vector<Parameter>::iterator iter = source_b_machine->locals.begin();
		for (unsigned int i=0; i<track_b.index; ++i, iter++) {;}
		source_b_machine->locals.erase(iter);
	}
#ifdef DEPENDENCYFIX
    if (last_machine_a && !remove_selected) {
        if (MachineIncludesParameter(source_a_machine,last_a)) {
            source_a_machine->addDependancy(last_machine_a);
            last_machine_a->listenTo(source_a_machine);
            last_machine_a->addDependancy(source_a_machine);
        }
    }
    if (last_machine_b && !remove_selected) {
        if (MachineIncludesParameter(source_b_machine,last_b)) {
            source_b_machine->addDependancy(last_machine_b);
            last_machine_b->listenTo(source_b_machine);
            last_machine_b->addDependancy(source_b_machine);
        }
    }
#endif
	debugParameterChange(dest_machine);
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
	debugParameterChange(dest_machine);
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
     */
}

Action::Status SelectSetOperation::doOperation() {
    int num_copied = 0;
    long to_copy;
	if (!source_a_machine) {
		error_str = "No source machine for copy";
		status = Failed;
		return status;
	}

	{
		if (start_pos.kind == Value::t_symbol || start_pos.kind == Value::t_string) {
			Value v = scope->properties.lookup(start_pos.sValue.c_str());
			if (!v.asInteger(sp)) sp = -1;
		}
		else {
			if (!start_pos.asInteger(sp)) sp = -1;
		}
		if (sp == -1) sp = 0;

		if (end_pos.kind == Value::t_symbol || end_pos.kind == Value::t_string) {
			Value v = scope->properties.lookup(start_pos.sValue.c_str());
			if (!v.asInteger(ep)) ep = -1;
		}
		else {
			if (!end_pos.asInteger(ep)) ep = -1;
		}
		if (ep == -1) count = 1; else count = ep - sp + 1;
	}

    if (source_a_machine) {
        if (count < 0 || !count.asInteger(to_copy)) to_copy = source_a_machine->parameters.size();
#ifdef DEPENDENCYFIX
        Value last;
        MachineInstance *last_machine = 0;
#endif
		// find or create the index to be used for the ITEM reference
		bool keep_item = false;
		bool add_item = true;
		unsigned int idx = 0;
		while (idx < source_a_machine->locals.size()) {
			if (source_a_machine->locals[idx].val.asString() == "ITEM") {
				keep_item = true;
				add_item = false;
				break;
			}
			++idx;
		}

		unsigned int i = sp; // start from here
		while (i < source_a_machine->parameters.size()) {
			debugParameterChange(dest_machine);
			Value a(source_a_machine->parameters.at(i).val);
			if (a.kind == Value::t_symbol) {
					MachineInstance *mi = owner->lookup(a);
#ifdef DEPENDENCYFIX
					if (last_machine && !remove_selected) {
							if (MachineIncludesParameter(source_a_machine,last)) {
									source_a_machine->addDependancy(last_machine);
									last_machine->listenTo(source_a_machine);
									last_machine->addDependancy(source_a_machine);
							}
					}
					last = a;
					last_machine = mi;
#endif
                //source_a_machine->addLocal(a, mi);
				if (add_item) {
					source_a_machine->locals.push_back(a);
					add_item = false;
				}
				else
					source_a_machine->locals[idx] = a;
					source_a_machine->locals[idx].machine = mi;
					source_a_machine->locals[idx].val.cached_machine = mi;

					source_a_machine->locals[idx].val = Value("ITEM");
					source_a_machine->locals[idx].real_name = a.sValue;
					//std::cout << "step " << i << " " << source_a_machine->locals[0].real_name << "\n";
					if (!mi)
						throw new SetOperationException();
					if (condition.predicate) {
						//std::cout << "flushing predicate cache\n";
						condition.predicate->flushCache();
						source_a_machine->localised_names["ITEM"] = mi;
					}
				
					if ( (!condition.predicate || condition(owner)) ){
						if (dest_machine->_type == "LIST") {
							if (!MachineIncludesParameter(dest_machine,a)) {
								dest_machine->addParameter(a);
							}
							++num_copied;
						}
						else if (dest_machine->_type == "REFERENCE") {
							if (dest_machine->locals.size()) dest_machine->removeLocal(0);
							dest_machine->addLocal("ITEM", mi);
							++num_copied;
						}
						if (remove_selected) {
								source_a_machine->removeParameter(i);
						}
						if (num_copied >= to_copy) break;
						if (remove_selected) continue; // skip the increment to next parameter
					}
					else if (condition.predicate) {
					//std::cout << "evaluation of " << condition.last_evaluation 
					//	<< " gave " << condition.last_result << "\n";
					}
				}
				else {
						if (!MachineIncludesParameter(dest_machine,a)) dest_machine->addParameter(a);
				}
				++i;
			}
		if (!add_item && !keep_item && source_a_machine->locals.size()) {
			std::vector<Parameter>::iterator iter = source_a_machine->locals.begin();
			for (unsigned int i=0; i<idx; ++i, iter++) {;}
			source_a_machine->locals.erase(iter);
		}
#ifdef DEPENDENCYFIX
        if (last_machine && !remove_selected) {
            if (MachineIncludesParameter(source_a_machine,last)) {
                source_a_machine->addDependancy(last_machine);
                last_machine->listenTo(source_a_machine);
                last_machine->addDependancy(source_a_machine);
            }
        }
#endif
    }
    std::stringstream ss;
    const char *delim="";
    ss << "[";
    for (unsigned int i=0; i<dest_machine->parameters.size(); ++i) {
        ss << delim << dest_machine->parameters[i].val;
        delim = ",";
    }
    ss << "]";
	source_a_machine->localised_names.erase("ITEM");
    dest_machine->setValue("DEBUG", ss.str().c_str());
    status = Complete;
    return status;
}


