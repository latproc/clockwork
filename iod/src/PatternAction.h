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

#ifndef __PATTERN_ACTION_H
#define __PATTERN_ACTION_H

#include "Action.h"

class MachineInstance;

struct CopyPatternActionTemplate : public ActionTemplate {
    CopyPatternActionTemplate(CStringHolder d, CStringHolder p, CStringHolder prop)
        : dest(d), pattern(p), property(prop) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "copy " << pattern.get() << " from " << property.get();
    }
    CStringHolder dest;
    CStringHolder pattern;
    CStringHolder property;
};

struct CopyPatternAction : public Action {
    CopyPatternAction(MachineInstance *mi, CopyPatternActionTemplate &pat)
        : Action(mi), dest(pat.dest.get()), pattern(pat.pattern.get()),
          property(pat.property.get()) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    std::string dest;
    std::string pattern;
    std::string property;
};

struct CopyAllPatternActionTemplate : public ActionTemplate {
    CopyAllPatternActionTemplate(CStringHolder d, CStringHolder p, CStringHolder prop)
        : dest(d), pattern(p), property(prop) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "copy " << pattern.get() << " from " << property.get();
    }
    CStringHolder dest;
    CStringHolder pattern;
    CStringHolder property;
};

struct CopyAllPatternAction : public Action {
    CopyAllPatternAction(MachineInstance *mi, CopyAllPatternActionTemplate &pat)
        : Action(mi), dest(pat.dest.get()), pattern(pat.pattern.get()),
          property(pat.property.get()) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    std::string dest;
    std::string pattern;
    std::string property;
};

struct ExtractPatternActionTemplate : public ActionTemplate {
    ExtractPatternActionTemplate(CStringHolder d, CStringHolder p, CStringHolder prop)
        : dest(d), pattern(p), property(prop) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "copy " << pattern.get() << " from " << property.get();
    }
    CStringHolder dest;
    CStringHolder pattern;
    CStringHolder property;
};

struct ExtractPatternAction : public Action {
    ExtractPatternAction(MachineInstance *mi, ExtractPatternActionTemplate &pat)
        : Action(mi), dest(pat.dest.get()), pattern(pat.pattern.get()),
          property(pat.property.get()) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    std::string dest;
    std::string pattern;
    std::string property;
};

struct ExtractAllPatternActionTemplate : public ActionTemplate {
    ExtractAllPatternActionTemplate(CStringHolder d, CStringHolder p, CStringHolder prop)
        : dest(d), pattern(p), property(prop) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "copy " << pattern.get() << " from " << property.get();
    }
    CStringHolder dest;
    CStringHolder pattern;
    CStringHolder property;
};

struct ExtractAllPatternAction : public Action {
    ExtractAllPatternAction(MachineInstance *mi, ExtractAllPatternActionTemplate &pat)
        : Action(mi), dest(pat.dest.get()), pattern(pat.pattern.get()),
          property(pat.property.get()) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    std::string dest;
    std::string pattern;
    std::string property;
};

struct ReplacePatternActionTemplate : public ActionTemplate {
    ReplacePatternActionTemplate(CStringHolder p, CStringHolder prop, CStringHolder repl)
        : pattern(p), property(prop), replacement(repl) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "replace " << pattern.get() << " in " << property.get() << " with "
                   << replacement.get();
    }
    CStringHolder pattern;
    CStringHolder property;
    CStringHolder replacement;
};

struct ReplacePatternAction : public Action {
    ReplacePatternAction(MachineInstance *mi, ReplacePatternActionTemplate &pat)
        : Action(mi), pattern(pat.pattern.get()), property(pat.property.get()),
          replacement(pat.replacement.get()) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    std::string pattern;
    std::string property;
    std::string replacement;
};

struct ReplaceAllPatternActionTemplate : public ActionTemplate {
    ReplaceAllPatternActionTemplate(CStringHolder p, CStringHolder prop, CStringHolder repl)
        : pattern(p), property(prop), replacement(repl) {}
    virtual Action *factory(MachineInstance *mi);
    std::ostream &operator<<(std::ostream &out) const {
        return out << "replace all" << pattern.get() << " in " << property.get() << " with "
                   << replacement.get();
    }
    CStringHolder pattern;
    CStringHolder property;
    CStringHolder replacement;
};

struct ReplaceAllPatternAction : public Action {
    ReplaceAllPatternAction(MachineInstance *mi, ReplaceAllPatternActionTemplate &pat)
        : Action(mi), pattern(pat.pattern.get()), property(pat.property.get()),
          replacement(pat.replacement.get()) {}
    Status run();
    Status checkComplete();
    virtual std::ostream &operator<<(std::ostream &out) const;
    std::string pattern;
    std::string property;
    std::string replacement;
};

#endif
