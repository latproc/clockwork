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

#include "QueryAction.h"
#include "cJSON.h"
#include "MachineInstance.h"
#include "SendMessageAction.h"
#include "value.h"

QueryActionTemplate::QueryActionTemplate(Value q, Value list)
    : query(q), list_name(list) {}

Action *QueryActionTemplate::factory(MachineInstance *mi) {
    return new QueryAction(mi, *this);
}

std::ostream &QueryActionTemplate::operator<<(std::ostream &out) const {
    return out << "QueryActionTemplate " << query << " INTO " << list_name << "\n";
}

QueryAction::QueryAction(MachineInstance *mi, QueryActionTemplate &qat)
    : Action(mi), query(qat.query), list_name(qat.list_name) {}

std::ostream &QueryAction::operator<<(std::ostream &out) const {
    return out << owner->getName() << ": QUERY " << query << " INTO " << list_name << "\n";
}

Action::Status QueryAction::run() {
    owner->start(this);

    // Resolve the query JSON: a symbol names an OPTION (JSON_VALUE) on the
    // owner; anything else (an inline JSON literal) is used directly.
    Value json_val = query;
    if (query.kind == Value::t_symbol) {
        Value resolved = owner->getValue(query);
        if (resolved != SymbolTable::Null) {
            json_val = resolved;
        }
    }

    // Build a copy of the query JSON with `respond_to` pointing at this
    // machine's `response` OPTION, so dbd routes the reply back here.
    cJSON *obj = nullptr;
    if (json_val.kind == Value::t_json && json_val.json) {
        obj = clone_json(json_val.json);
    }
    else {
        obj = cJSON_Parse(json_val.asString().c_str());
    }
    std::string msg_str;
    if (obj && obj->type == cJSON_Object) {
        cJSON_DeleteItemFromObject(obj, "respond_to");
        cJSON_AddStringToObject(obj, "respond_to", (owner->getName() + ".response").c_str());
        char *out_s = cJSON_PrintUnformatted(obj);
        msg_str = out_s ? out_s : "";
        free(out_s);
    }
    else {
        msg_str = json_val.asString();
    }
    cJSON_Delete(obj);

    // Reuse the SEND-to-channel machinery for the actual delivery.
    SendMessageActionTemplate smat(Value(msg_str, Value::t_string), Value("DATABASE_CHANNEL"));
    Action *send = smat.factory(owner);
    Status s = send ? (*send)() : Action::Failed;
    delete send;

    owner->stop(this);
    status = s;
    return s;
}

Action::Status QueryAction::checkComplete() { return Action::Complete; }
