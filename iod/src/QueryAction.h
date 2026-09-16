/*
    Copyright (C) 2024 Martin Leadbeater, Michael O'Connor

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

#pragma once
#include "Action.h"
#include "symboltable.h"

class MachineInstance;

// QUERY <json> INTO <list>
//
// Sends the query JSON to DATABASE_CHANNEL, injecting a `respond_to` field that
// routes the dbsvr reply back to the issuing machine's `response` OPTION. The
// scan cannot wait for dbsvr, so the SEND does not fill the list synchronously.
// The parser installs a synthetic `response_changed` handler for the INTO target
// (see cwlang.ypp); when the reply lands it runs QueryFillListAction, which
// clears the named LIST and refills it from `response`. If the machine declares
// its own `RECEIVE response_changed`, that handler wins and no synthetic fill is
// installed, so programs written around the older "INTO is a hint" model (fill
// the list yourself in the handler, then drain it) are unchanged.
struct QueryActionTemplate : public ActionTemplate {
    QueryActionTemplate(Value query, Value list_name);
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value query;     // a symbol (OPTION holding the JSON) or an inline JSON value
    Value list_name; // INTO target
};

struct QueryAction : public Action {
    QueryAction(MachineInstance *mi, QueryActionTemplate &qat);
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value query;
    Value list_name;
};

// The runtime half of the automatic QUERY ... INTO fill. On the reply's
// `response_changed` it replaces the contents of the named LIST with the elements
// of the source JSON array (the machine's `response` OPTION by default). A target
// that is not a LIST (a RECORD, or an unknown name) is left untouched.
struct QueryFillListActionTemplate : public ActionTemplate {
    QueryFillListActionTemplate(Value list_name, Value source_name);
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value list_name;
    Value source_name;
};

struct QueryFillListAction : public Action {
    QueryFillListAction(MachineInstance *mi, QueryFillListActionTemplate &qfat);
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value list_name;
    Value source_name;
};
