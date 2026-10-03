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

#include "MachineGraph.h"

#include "Expression.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "Message.h"
#include "StableState.h"
#include "symboltable.h"
#include "Transition.h"
#include "value.h"

#include <algorithm>
#include <list>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool instanceNameLess(MachineInstance *a, MachineInstance *b) {
    return a->getName() < b->getName();
}

// Graphviz quoted-ID escaping. Guard text legitimately contains '|', '||', '<'
// and '>', all of which are meaningful inside an unquoted or record label, so
// every id and label goes through here.
std::string dotEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string q(const std::string &s) { return "\"" + dotEscape(s) + "\""; }

std::string quoteValue(const Value &v) { return q(v.asString()); }

// Node identity is the full name (owner chain included). Two instances nested
// under different owners can share a short name - a local declared in a class
// body is instantiated once per owner - and using the short name alone would
// merge them into one Graphviz node. `label` stays the short name so rendering
// is unchanged for the top-level instances that make up most graphs.
std::string instanceId(MachineInstance *mi) { return q(mi->fullName()); }

std::string classId(MachineClass *mc) { return q(std::string("class:") + mc->name); }

std::string optionId(MachineClass *mc, const std::string &opt) {
    return q(std::string("class:") + mc->name + "::opt:" + opt);
}

std::string rulesId(MachineClass *mc) {
    return q(std::string("class:") + mc->name + "::rules");
}

std::string stateId(MachineClass *mc, const std::string &state) {
    return q(std::string("class:") + mc->name + "::state:" + state);
}

std::string propertyId(MachineInstance *mi, const std::string &prop) {
    return q(std::string("prop:") + mi->fullName() + "::" + prop);
}

// True when the class declares an ENTER (or LEAVE) action block for the state.
// The parser stores these in `receives` under "<state>_enter" / "<state>_leave".
bool hasStateActions(MachineClass *mc, const std::string &state, bool enter) {
    std::string key = state + (enter ? "_enter" : "_leave");
    for (std::multimap<Message, MachineCommandTemplate *>::const_iterator it = mc->receives.begin();
         it != mc->receives.end(); ++it) {
        if (it->first.getText() != key) {
            continue;
        }
        if (enter ? it->first.isEnter() : it->first.isLeave()) {
            return true;
        }
    }
    return false;
}

std::string conditionText(Condition *c) {
    if (!c || !c->predicate) {
        return "";
    }
    std::ostringstream ss;
    ss << *c->predicate;
    return ss.str();
}

std::string predicateText(Predicate *p) {
    if (!p) {
        return "";
    }
    std::ostringstream ss;
    ss << *p;
    return ss.str();
}

// The connected component containing `seed`: parameter bindings both ways, the
// owner chain, and depends both ways. The previous exporters walked parameters
// only, so `-r` silently dropped depends relations.
void collectComponent(std::set<MachineInstance *> &seen, MachineInstance *mi) {
    if (!mi || seen.find(mi) != seen.end()) {
        return;
    }
    seen.insert(mi);

    for (size_t i = 0; i < mi->parameters.size(); ++i) {
        if (mi->parameters[i].machine) {
            collectComponent(seen, mi->parameters[i].machine);
        }
    }
    if (mi->owner) {
        collectComponent(seen, mi->owner);
    }
    for (std::set<MachineInstance *>::const_iterator d = mi->depends.begin();
         d != mi->depends.end(); ++d) {
        collectComponent(seen, *d);
    }

    // Reverse relations: instances that use or depend on this one.
    for (std::list<MachineInstance *>::iterator it = MachineInstance::begin();
         it != MachineInstance::end(); ++it) {
        MachineInstance *other = *it;
        if (seen.find(other) != seen.end()) {
            continue;
        }
        bool linked = (other->owner == mi) || (other->depends.find(mi) != other->depends.end());
        for (size_t i = 0; i < other->parameters.size() && !linked; ++i) {
            if (other->parameters[i].machine == mi) {
                linked = true;
            }
        }
        if (linked) {
            collectComponent(seen, other);
        }
    }
}

} // namespace

namespace MachineGraph {

std::ostream &writeDot(std::ostream &out, const Options &opts) {
    std::set<MachineInstance *> included;
    if (opts.root) {
        MachineInstance *seed = MachineInstance::find(opts.root);
        if (seed) {
            collectComponent(included, seed);
        }
    }

    out << "digraph G {\n";
    out << "  node [shape=box];\n";

    // Machine instances.
    for (std::list<MachineInstance *>::iterator it = MachineInstance::begin();
         it != MachineInstance::end(); ++it) {
        MachineInstance *mi = *it;
        if (opts.root && included.find(mi) == included.end()) {
            continue;
        }
        out << "  " << instanceId(mi) << " [label=" << q(mi->getName())
            << ", name=" << q(mi->getName());
        MachineClass *mc = mi->getStateMachine();
        if (mc) {
            out << ", class=" << q(mc->name);
        }
        if (mc) {
            out << ", state=" << q(mi->getCurrentStateString());
        }
        out << ", enabled=" << q(mi->enabled() ? "true" : "false");
        if (!mi->definition_file.empty()) {
            out << ", file=" << q(mi->definition_file);
            out << ", line=" << q(std::to_string(mi->definition_line));
        }
        out << "];\n";
    }

    // Classes, options and per-instance property overrides. Collected first so
    // the class block is emitted once, in name order, and is deterministic.
    std::map<std::string, MachineClass *> classes;
    for (std::list<MachineInstance *>::iterator it = MachineInstance::begin();
         it != MachineInstance::end(); ++it) {
        MachineInstance *mi = *it;
        if (opts.root && included.find(mi) == included.end()) {
            continue;
        }
        MachineClass *mc = mi->getStateMachine();
        if (mc) {
            classes[mc->name] = mc;
        }
    }

    for (std::map<std::string, MachineClass *>::iterator c = classes.begin(); c != classes.end();
         ++c) {
        MachineClass *mc = c->second;
        out << "  " << classId(mc) << " [label=" << q("class " + mc->name)
            << ", style=dashed, shape=box];\n";
    }
    for (std::list<MachineInstance *>::iterator it = MachineInstance::begin();
         it != MachineInstance::end(); ++it) {
        MachineInstance *mi = *it;
        if (opts.root && included.find(mi) == included.end()) {
            continue;
        }
        MachineClass *mc = mi->getStateMachine();
        if (mc) {
            out << "  " << instanceId(mi) << " -> " << classId(mc)
                << " [label=\"instance_of\"];\n";
        }
    }

    if (opts.values) {
        // Class OPTIONs: class-level constants, fixed at definition.
        for (std::map<std::string, MachineClass *>::iterator c = classes.begin(); c != classes.end();
             ++c) {
            MachineClass *mc = c->second;
            const std::map<std::string, Value> &options = mc->getOptions();
            for (std::map<std::string, Value>::const_iterator o = options.begin();
                 o != options.end(); ++o) {
                out << "  " << optionId(mc, o->first)
                    << " [shape=note, label=" << q(o->first + " = " + o->second.asString()) << "];\n";
                out << "  " << optionId(mc, o->first) << " -> " << classId(mc)
                    << " [label=\"option\"];\n";
            }
        }

        // Per-instance property overrides. A node is written only where the class
        // declares the property and this instance carries a different value -
        // those are the values the instance was constructed with, which is where
        // an instance diverges from the class body a reader is looking at.
        // Class OPTIONs are deliberately not repeated here (they are class-level
        // constants, written above), nor are auto-generated properties such as
        // NAME that no class declares.
        for (std::list<MachineInstance *>::iterator it = MachineInstance::begin();
             it != MachineInstance::end(); ++it) {
            MachineInstance *mi = *it;
            if (opts.root && included.find(mi) == included.end()) {
                continue;
            }
            MachineClass *mc = mi->getStateMachine();
            static const SymbolTable no_defaults;
            const SymbolTable &defaults = mc ? mc->getProperties() : no_defaults;
            for (SymbolTableConstIterator p = mi->properties.begin(); p != mi->properties.end();
                 ++p) {
                if (!defaults.exists(p->first.c_str())) {
                    continue;
                }
                if (defaults.lookup(p->first.c_str()) == p->second) {
                    continue;
                }
                out << "  " << propertyId(mi, p->first)
                    << " [shape=note, label=" << q(p->first + " = " + p->second.asString()) << "];\n";
                out << "  " << propertyId(mi, p->first) << " -> " << instanceId(mi)
                    << " [label=\"property\"];\n";
            }
        }
    }

    // Instance relations. The parameter edges keep the direction the exporter has
    // always used (parameter machine -> user) so existing consumers still match.
    for (std::list<MachineInstance *>::iterator it = MachineInstance::begin();
         it != MachineInstance::end(); ++it) {
        MachineInstance *mi = *it;
        if (opts.root && included.find(mi) == included.end()) {
            continue;
        }
        for (size_t i = 0; i < mi->parameters.size(); ++i) {
            MachineInstance *param = mi->parameters[i].machine;
            if (param && (!opts.root || included.find(param) != included.end())) {
                out << "  " << instanceId(param) << " -> " << instanceId(mi)
                    << " [label=\"parameter\"];\n";
            }
        }
        // depends is a pointer-keyed set, so sort by name for stable output.
        std::vector<MachineInstance *> deps(mi->depends.begin(), mi->depends.end());
        std::sort(deps.begin(), deps.end(), instanceNameLess);
        for (size_t i = 0; i < deps.size(); ++i) {
            if (opts.root && included.find(deps[i]) == included.end()) {
                continue;
            }
            out << "  " << instanceId(mi) << " -> " << instanceId(deps[i])
                << " [label=\"depends\"];\n";
        }
        if (mi->owner && (!opts.root || included.find(mi->owner) != included.end())) {
            out << "  " << instanceId(mi) << " -> " << instanceId(mi->owner)
                << " [label=\"owner\"];\n";
        }
    }

    if (opts.rules) {
        for (std::map<std::string, MachineClass *>::iterator c = classes.begin(); c != classes.end();
             ++c) {
            MachineClass *mc = c->second;
            if (mc->stable_states.empty() && mc->transitions.empty()) {
                continue;
            }

            // WHEN rules. These carry no source state: the rule fires from
            // wherever the machine is, so they hang off a per-class rule node in
            // evaluation order. `rule` is the order the interpreter tests them.
            std::set<std::string> states;
            for (size_t i = 0; i < mc->stable_states.size(); ++i) {
                states.insert(mc->stable_states[i].state_name);
            }
            for (std::list<Transition>::const_iterator t = mc->transitions.begin();
                 t != mc->transitions.end(); ++t) {
                states.insert(t->source.getName());
                states.insert(t->dest.getName());
            }
            for (std::set<std::string>::const_iterator s = states.begin(); s != states.end(); ++s) {
                out << "  " << stateId(mc, *s) << " [shape=ellipse, label=" << q(*s) << "];\n";
            }

            if (!mc->stable_states.empty()) {
                out << "  " << rulesId(mc) << " [shape=point, label=\"\"];\n";
            }
            for (size_t i = 0; i < mc->stable_states.size(); ++i) {
                const StableState &ss = mc->stable_states[i];
                out << "  " << rulesId(mc) << " -> " << stateId(mc, ss.state_name) << " [rule="
                    << q(std::to_string(i)) << ", kind=\"when\""
                    << ", enter=" << q(hasStateActions(mc, ss.state_name, true) ? "true" : "false")
                    << ", leave=" << q(hasStateActions(mc, ss.state_name, false) ? "true" : "false")
                    << ", label=" << q(predicateText(ss.condition.predicate)) << "];\n";
            }

            // TRANSITION statements, which do have a source state.
            size_t index = mc->stable_states.size();
            for (std::list<Transition>::const_iterator t = mc->transitions.begin();
                 t != mc->transitions.end(); ++t) {
                std::string label = t->trigger.getText();
                std::string guard = conditionText(t->condition);
                if (!guard.empty()) {
                    label += " / " + guard;
                }
                out << "  " << stateId(mc, t->source.getName()) << " -> "
                    << stateId(mc, t->dest.getName()) << " [rule=" << q(std::to_string(index))
                    << ", kind=\"transition\""
                    << ", trigger=" << q(t->trigger.getText())
                    << ", guard=" << q(guard) << ", label=" << q(label) << "];\n";
                ++index;
            }
        }
    }

    out << "}\n";
    return out;
}

} // namespace MachineGraph
