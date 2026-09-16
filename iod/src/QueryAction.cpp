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
#include "ClearListAction.h"
#include "IncludeAction.h"
#include "Logger.h"
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

QueryFillListActionTemplate::QueryFillListActionTemplate(Value list_name, Value source_name)
    : list_name(list_name), source_name(source_name) {}

Action *QueryFillListActionTemplate::factory(MachineInstance *mi) {
    return new QueryFillListAction(mi, *this);
}

std::ostream &QueryFillListActionTemplate::operator<<(std::ostream &out) const {
    return out << "QueryFillListActionTemplate " << list_name << " FROM " << source_name << "\n";
}

QueryFillListAction::QueryFillListAction(MachineInstance *mi, QueryFillListActionTemplate &qfat)
    : Action(mi), list_name(qfat.list_name), source_name(qfat.source_name) {}

std::ostream &QueryFillListAction::operator<<(std::ostream &out) const {
    return out << owner->getName() << ": fill " << list_name << " FROM " << source_name << "\n";
}

Action::Status QueryFillListAction::run() {
    owner->start(this);

    MachineInstance *list_machine = owner->lookup(list_name.asString());
    if (!list_machine || list_machine->_type != "LIST") {
        // QUERY ... INTO a RECORD (or a name that is not a LIST) keeps the old
        // behaviour: the reply stays on `response` only.
        status = Complete;
        owner->stop(this);
        return status;
    }

    // Resolve the source: a symbol names a property on the owner (normally the
    // machine's `response` OPTION); anything else is used as-is.
    Value src = source_name;
    if (source_name.kind == Value::t_symbol) {
        Value resolved = owner->getValue(source_name);
        if (resolved != SymbolTable::Null) {
            src = resolved;
        }
    }

    // Replace the list contents, matching `list := <json> AS LIST`: clear first.
    // A reply that is not a JSON array (an error string, say) leaves the list
    // empty rather than failing the receive handler.
    clearListContents(list_machine);
    if (src.kind == Value::t_json && src.json && src.json->type == cJSON_Array) {
        add_json_array(list_machine, src, -1, false);
    }

    status = Complete;
    owner->stop(this);
    return status;
}

Action::Status QueryFillListAction::checkComplete() {
    if (status == Complete || status == Failed) {
        return status;
    }
    if (this != owner->executingCommand()) {
        DBG_MSG << "checking complete on " << *this << " when it is not the top of stack \n";
    }
    else {
        status = Complete;
        owner->stop(this);
    }
    return status;
}

