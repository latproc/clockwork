#include "UpdateListItemsAction.h"

#include "Logger.h"
#include "MessageLog.h"
#include "symboltable.h"
#include <sstream>
#include <vector>
#include "DebugExtra.h"

UpdateListItemsActionTemplate::UpdateListItemsActionTemplate(const char *property_name,
                                                             const char *delim,
                                                             const char *sourceSymbol,
                                                             const char *listSymbol)
    : property(property_name ? Value{property_name, Value::t_string} : SymbolTable::Null),
      delimiter(delim, Value::t_string)
    , source_symbol(sourceSymbol)
    , list_symbol(listSymbol) {}

Action *UpdateListItemsActionTemplate::factory(MachineInstance *mi) {
    return new UpdateListItemsAction(mi, *this);
}

std::ostream &UpdateListItemsActionTemplate::operator<<(std::ostream &out) const {
    return out << "UpdateListItemsActionTemplate property=\"" << property << "\" delim=\"" << delimiter.asString() << "\" "
               << "from=" << source_symbol << " to LIST " << list_symbol;
}

void UpdateListItemsActionTemplate::toC(std::ostream &out, std::ostream &vars) const {
    // If your C exporter doesn’t need this yet, a stub is fine.
    out << "\t/* DESERIALISE STATE not implemented in C export yet */\n";
}

static std::vector<std::string> split_string(const std::string &s,
                                             const std::string &delim) {
    std::vector<std::string> result;
    if (delim.empty()) {
        result.push_back(s);
        return result;
    }

    std::string::size_type start = 0;
    while (true) {
        auto pos = s.find(delim, start);
        if (pos == std::string::npos) {
            result.emplace_back(s.substr(start));
            break;
        }
        result.emplace_back(s.substr(start, pos - start));
        start = pos + delim.size();
    }
    return result;
}

UpdateListItemsAction::UpdateListItemsAction(MachineInstance *mi,
                                             UpdateListItemsActionTemplate &tmpl)
    : Action(mi)
    , delimiter(tmpl.delimiter)
    , source_symbol(tmpl.source_symbol)
    , list_symbol(tmpl.list_symbol)
    , list_machine(0) {}

Action::Status UpdateListItemsAction::run() {
    owner->start(this);

    list_symbol.cached_machine = 0; // clear any stale cache, mirroring SendMessageAction
    list_machine = owner->lookup(list_symbol);

    if (!list_machine) {
        std::stringstream ss;
        ss << *this << " Error: cannot find LIST machine " << list_symbol;
        MessageLog::instance()->add(ss.str().c_str());
        NB_MSG << ss.str() << "\n";
        status = Action::Failed;
        owner->stop(this);
        return status;
    }

    if (list_machine->_type != "LIST") {
        std::stringstream ss;
        ss << *this << " Error: target " << list_symbol
           << " is not a LIST (type=" << list_machine->_type << ")";
        MessageLog::instance()->add(ss.str().c_str());
        NB_MSG << ss.str() << "\n";
        status = Action::Failed;
        owner->stop(this);
        return status;
    }

    std::string serialised;
    if (source_symbol.kind == Value::t_symbol) {
        Value v = owner->getValue(source_symbol);
        if (v != SymbolTable::Null) {
            serialised = v.asString();
        }
        else {
            serialised.clear();
        }
    }
    else {
        // Shouldn't happen from the grammar, but be robust
        serialised = source_symbol.asString();
    }

    const std::string delim = delimiter.asString();
    std::vector<std::string> tokens = split_string(serialised, delim);

    bool use_state_if_no_property = false;
    if (property.kind == Value::t_empty) {
        use_state_if_no_property = true;
        property = Value{"VALUE", Value::t_string};
    }

    // Preflight check that all machines in the list have the destination state or property
    struct StateChange {
        MachineInstance *machine;
        const State *new_state;
        Value property;
        Value value;
    };
    const std::size_t n = list_machine->parameters.size();
    std::vector<StateChange> pending;
    pending.reserve(n);
    bool setting_properties = false;
    bool setting_states = false;
    for (std::size_t i = 0; i < n && i < tokens.size(); ++i) {
        MachineInstance *entry = list_machine->parameters[i].machine;
        if (!entry) {
            continue;
        }

        const std::string &state_name = tokens[i];

        if (state_name.empty()) {
            continue; // skip empty tokens; you can change this if you want "empty" to mean something
        }

        DBG_ACTIONS << owner->getName() << ": UpdateListItemsAction set "
                    << entry->fullName() << " STATE=" << state_name << "\n";

        if (property == "VALUE") {
            Value val{entry->getValue(property.asString().c_str())};
            if (val.kind == Value::t_empty && use_state_if_no_property) {
                property = "STATE";
            }
            else {
                if (setting_states) {
                    std::stringstream ss;
                    ss << owner->getName() << ": Inconsistent deserialise, some items are setting states";
                    MessageLog::instance()->add(ss.str().c_str());
                }
                else {
                    pending.push_back({entry, nullptr, property, state_name});
                    setting_properties = true;
                }
            }
        }
        if (property == "STATE") {
            const State *s = entry->getStateMachine()->findState(state_name.c_str());
            if (s) {
                if (setting_properties) {
                    std::stringstream ss;
                    ss << owner->getName() << ": Inconsistent deserialise, some items are using properties but not all items have the property";
                    MessageLog::instance()->add(ss.str().c_str());
                }
                else {
                    pending.push_back({entry, s, SymbolTable::Null});
                    setting_states = true;
                }

            }
            else {
                std::stringstream ss;
                ss << *this << " Failed to find state " << state_name << " in machine " << list_machine->_type;
                MessageLog::instance()->add(ss.str().c_str());
            }
        }
    }
    if (pending.size() == n) {
        for (auto item : pending) {
            if (item.new_state) {
                auto status = item.machine->setState(*item.new_state);
                if (status == Action::Failed) {
                    std::stringstream ss;
                    ss << item.machine->getName() << " Failed to set state to " << item.new_state->getName();
                    MessageLog::instance()->add(ss.str().c_str());
                }
            }
            else {
                item.machine->setValue(item.property.asString(),item.value);
            }
        }
    }

    status = Action::Complete;
    owner->stop(this);
    return status;
}

std::ostream &UpdateListItemsAction::operator<<(std::ostream &out) const {
    return out << owner->getName() << ": UpdateListItemsAction "
               << "from=" << source_symbol << " to LIST " << list_symbol;
}