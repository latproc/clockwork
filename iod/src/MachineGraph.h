#ifndef cwlang_MachineGraph_h
#define cwlang_MachineGraph_h

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

#include <iosfwd>

/* Dependency graph export (the -g FILE option of cw / iod / iod_sdo / iod-elc).

   The graph is written as a single Graphviz DOT digraph. Node and edge kinds
   carry attributes rather than distinct files, so one artifact answers the
   structural questions (what is bound to what) and the order/guard/configuration
   questions that a bare instance-to-instance edge list cannot express:

     - machine instances        node, attributes class/state/enabled/file/line
     - machine classes          node class:NAME, reached by an instance_of edge
     - class OPTIONs            node class:NAME::opt:OPTION, value in the label
     - per-instance overrides   node prop:INSTANCE::PROPERTY, emitted only where
                                the instance value differs from the class default
     - instance relations       edges labelled parameter, depends, owner
     - WHEN rules               edges class:NAME::rules -> state, in evaluation
                                order, with rule index, guard text and whether the
                                state has ENTER / LEAVE actions
     - TRANSITION statements    edges state -> state, with rule index, trigger and
                                REQUIRES guard text

   Rule order is the order the interpreter evaluates, i.e. MachineClass holds
   stable_states after semantic_analysis() has moved the DEFAULT rule to the end.
   The node [shape=...] default is box rather than record because guard text can
   contain '|' and '||', which record shapes treat as field separators.

   See iod/docs/DEPENDENCY_GRAPH.md.
*/

namespace MachineGraph {

struct Options {
    // Restrict the output to the connected component containing this instance.
    // nullptr (the default) writes every loaded instance.
    const char *root;
    // Write class rule lists (WHEN rules and TRANSITION statements).
    bool rules;
    // Write class OPTIONs and per-instance property overrides.
    bool values;

    Options() : root(0), rules(true), values(true) {}
};

// Writes the graph for the currently loaded configuration. The caller is
// responsible for having loaded a program (and for having run semantic
// analysis, which loadConfig does).
std::ostream &writeDot(std::ostream &out, const Options &opts = Options());

} // namespace MachineGraph

#endif
