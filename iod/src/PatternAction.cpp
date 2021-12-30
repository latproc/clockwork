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

#include "PatternAction.h"
#include "DebugExtra.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "regular_expressions.h"

Action *CopyPatternActionTemplate::factory(MachineInstance *mi) {
    return new CopyPatternAction(mi, *this);
}

Action::Status CopyPatternAction::run() {
    owner->start(this);

    DBG_M_ACTIONS << " processing " << *this << "\n";
    status = Running;
    Value val;
    val = owner->getValue(property);
    std::vector<std::string> matches;
    rexp_info *info = create_pattern(pattern.c_str());
    find_matches(info, matches, val.asString().c_str());
    if (matches.size()) {
        if (matches.size() > 1) {
            owner->setValue(dest, Value(matches[1], Value::t_string));
        }
        else {
            owner->setValue(dest, Value(matches[0], Value::t_string));
        }
    }
    release_pattern(info);
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status CopyPatternAction::checkComplete() { return Complete; }

std::ostream &CopyPatternAction::operator<<(std::ostream &out) const {
    out << "copy " << pattern << " from " << property;
    return out;
}

Action *CopyAllPatternActionTemplate::factory(MachineInstance *mi) {
    return new CopyAllPatternAction(mi, *this);
}

static int matched(const char *match, int index, void *user_data) {
    std::vector<std::string> *matches = static_cast<std::vector<std::string> *>(user_data);
    if (index == 0) {
        matches->push_back(match);
    }
    return 0;
}

Action::Status CopyAllPatternAction::run() {
    owner->start(this);

    DBG_M_ACTIONS << " processing " << *this << "\n";
    status = Running;
    Value val;
    val = owner->getValue(property);
    std::vector<std::string> matches;
    rexp_info *info = create_pattern(pattern.c_str());
    each_match(info, val.asString().c_str(), 0, matched, &matches);
    if (matches.size()) {
        std::string res;
        for (unsigned int i = 0; i < matches.size(); ++i) {
            res += matches[i];
        }
        owner->setValue(dest, Value(res, Value::t_string));
    }
    release_pattern(info);
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status CopyAllPatternAction::checkComplete() { return Complete; }

std::ostream &CopyAllPatternAction::operator<<(std::ostream &out) const {
    out << "copy all " << pattern << " from " << property;
    return out;
}

Action *ExtractPatternActionTemplate::factory(MachineInstance *mi) {
    return new ExtractPatternAction(mi, *this);
}

Action::Status ExtractPatternAction::run() {
    owner->start(this);

    DBG_M_ACTIONS << " processing " << *this << "\n";
    status = Running;
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status ExtractPatternAction::checkComplete() { return Complete; }

std::ostream &ExtractPatternAction::operator<<(std::ostream &out) const {
    out << "extract " << pattern << " from " << property;
    return out;
}

Action *ExtractAllPatternActionTemplate::factory(MachineInstance *mi) {
    return new ExtractAllPatternAction(mi, *this);
}

Action::Status ExtractAllPatternAction::run() {
    owner->start(this);

    DBG_M_ACTIONS << " processing " << *this << "\n";
    status = Running;
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status ExtractAllPatternAction::checkComplete() { return Complete; }

std::ostream &ExtractAllPatternAction::operator<<(std::ostream &out) const {
    out << "extract all " << pattern << " from " << property;
    return out;
}

Action *ReplacePatternActionTemplate::factory(MachineInstance *mi) {
    return new ReplacePatternAction(mi, *this);
}

Action::Status ReplacePatternAction::run() {
    owner->start(this);

    DBG_M_ACTIONS << " processing " << *this << "\n";
    status = Running;
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status ReplacePatternAction::checkComplete() { return Complete; }

std::ostream &ReplacePatternAction::operator<<(std::ostream &out) const {
    out << "replace " << pattern << " in " << property << " with " << replacement;
    return out;
}

Action *ReplaceAllPatternActionTemplate::factory(MachineInstance *mi) {
    return new ReplaceAllPatternAction(mi, *this);
}

Action::Status ReplaceAllPatternAction::run() {
    owner->start(this);

    DBG_M_ACTIONS << " processing " << *this << "\n";
    status = Running;
    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status ReplaceAllPatternAction::checkComplete() { return Complete; }

std::ostream &ReplaceAllPatternAction::operator<<(std::ostream &out) const {
    out << "replace all " << pattern << " from " << property << " with " << replacement;
    return out;
}
