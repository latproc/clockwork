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
// `INTO <list>` target is a hint (Martin's design: QUERY returns JSON; the
// author turns the reply into the list with `list := response AS LIST` when the
// reply arrives) — the scan cannot wait for dbsvr, so this action does not fill
// the list itself.
struct QueryActionTemplate : public ActionTemplate {
    QueryActionTemplate(Value query, Value list_name);
    Action *factory(MachineInstance *mi) override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value query;      // a symbol (OPTION holding the JSON) or an inline JSON value
    Value list_name;  // INTO target (documentation/hint)
};

struct QueryAction : public Action {
    QueryAction(MachineInstance *mi, QueryActionTemplate &qat);
    Status run() override;
    Status checkComplete() override;
    std::ostream &operator<<(std::ostream &out) const override;
    Value query;
    Value list_name;
};
