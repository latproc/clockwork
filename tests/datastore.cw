# This is an example of interfacing clockwork to a database.

#db EXTERNAL(HOST:"localhost", PORT:8102, protocol:CLOCKWORK);

DATASTORE MACHINE {
    OPTION file "";
    OPTION result JSON_VALUE {};
}
#db DATASTORE (file: "clockwork.db");
    
customer_record Customer;
manager RecordManager customer_record;
raw_sql Sql;

Customer MACHINE {
    OPTION id 0;
    OPTION name "";
    OPTION email "";
    OPTION age 0;

    OPTION create JSON_VALUE {
        "action": "create",
        "type": "customer",
        "schema": {"id": "integer primary key", "name": "string", "age": "integer", "email": "string"}
    };
    OPTION insert JSON_VALUE {
         "action": "insert",
         "type": "customer",
         "data": {"name": "", "email": "", "age": 0}
    };
    OPTION find_by_id JSON_VALUE {
         "action": "find",
         "type": "customer",
         "keys": {"id": 0},
         "fields": ["id", "name", "email"]
    };
    OPTION find_by_name JSON_VALUE {
         "action": "find",
         "type": "customer",
         "keys": {"name": ""},
         "fields": ["id", "name", "email"]
    };
    OPTION update JSON_VALUE {
         "action": "update",
         "type": "customer",
         "keys": {"id": 0},
         "data": {"name": "", "email": "", "age": 0}
    };
    OPTION delete JSON_VALUE {
         "action": "delete",
         "type": "customer",
         "keys": {"id": 0}
    };

    # Result isn't supported yet.
    OPTION result JSON_VALUE [
        {"id": "id", "name": "name", "email": "email"}
    ];

}

# Don't overruse this, it doesn't support returning values but it can
# be used to do things like create indexes and drop or alter tables.
Sql MACHINE {
    OPTION sql_statement JSON_VALUE {"action": "sql", "sql": ""};
    OPTION sql ""; 
}

bale_list LIST;

BaleManager MACHINE bale_list{
    OPTION response;
    RECEIVE BaleRecord {
        bale_list := response;
        bale := TAKE FIRST FROM bale_list;
        LOG ITEM ${rfid} OF bale;
    }
}

BaleScanner MACHINE {
}

RecordManager MACHINE record {
    OPTION id 0;
    OPTION request JSON_VALUE {};
    OPTION response JSON_VALUE {
        "machine": "BaleManager",
        "message": "BaleRecord",
        "details": {"id": 0, "name": "", "email": "", "age": 0}
    };
    COMMAND update {
        request := record.update;
        ITEM ${keys.id} OF request := record.id;
        ITEM ${data.name} OF request := record.name;
        ITEM ${data.email} OF request := record.email;
        ITEM ${data.age} OF request := record.age;
        ITEM ${auth} OF request := "xxx";
        LOG request;
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND insert {
        request := record.insert;
        ITEM ${data.name} OF request := record.name;
        ITEM ${data.email} OF request := record.email;
        ITEM ${data.age} OF request := record.age;
        ITEM ${auth} OF request := "xxx";
        LOG request;
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND find_by_id {
        request := record.find_by_id;
        ITEM ${keys.id} OF request := record.id;
        ITEM ${auth} OF request := "xxx";
        ITEM ${respond_to} OF request := response;
        customer := LOOKUP USING request TO DATABASE_CHANNEL;
        SEND request TO DATABASE_CHANNEL;
        LOG request;
    }
    COMMAND do_sql {
        request := raw_sql.sql_statement;
        ITEM ${sql} OF request := raw_sql.sql;
        ITEM ${auth} OF request := "xxx";
        LOG request;
    }


}

