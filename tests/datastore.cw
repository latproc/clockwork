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
    OPTION find_all JSON_VALUE {
         "action": "find",
         "type": "customer",
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

item_list LIST;

ItemManager MACHINE list{
    OPTION response JSON_VALUE [];
    LOCAL OPTION rfid 0;
    RECEIVE ItemRecord {
        list := response;
        item := TAKE FIRST FROM list;
        rfid := ITEM ${rfid} OF item;
        LOG "rfid: " + rfid;
    }
}

RecordManager MACHINE record {
    OPTION id 0;
    OPTION request JSON_VALUE {};
    OPTION response JSON_VALUE {};
    OPTION data JSON_VALUE {};

    COMMAND update {
        request := record.update;
        ITEM ${keys.id} OF request := record.id;
        ITEM ${data.name} OF request := record.name;
        ITEM ${data.email} OF request := record.email;
        ITEM ${data.age} OF request := record.age;
        LOG request;
        ITEM ${auth} OF request := "xxx";
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND insert {
        request := record.insert;
        ITEM ${data.name} OF request := record.name;
        ITEM ${data.email} OF request := record.email;
        ITEM ${data.age} OF request := record.age;
        LOG request;
        ITEM ${auth} OF request := "xxx";
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND delete {
        request := record.delete;
        ITEM ${keys.id} OF request := record.id;
        LOG request;
        ITEM ${auth} OF request := "xxx";
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND find_all {
        request := record.find_all;
        # indicate where the result should be posted
        ITEM ${respond_to} OF request := SELF.NAME + ".response";
        LOG request;
        ITEM ${auth} OF request := "xxx";
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND find_by_id {
        request := record.find_by_id;
        ITEM ${keys.id} OF request := record.id;
        # indicate where the result should be posted
        ITEM ${respond_to} OF request := SELF.NAME + ".response";
        LOG request;
        ITEM ${auth} OF request := "xxx";
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND find_by_name {
        request := record.find_by_name;
        ITEM ${keys.name} OF request := record.name;
        # indicate where the result should be posted
        ITEM ${respond_to} OF request := SELF.NAME + ".response";
        LOG request;
        ITEM ${auth} OF request := "xxx";
        SEND request TO DATABASE_CHANNEL;
    }
    COMMAND do_sql {
        request := raw_sql.sql_statement;
        ITEM ${sql} OF request := raw_sql.sql;
        LOG request;
        ITEM ${auth} OF request := "xxx";
    }

    # After the database service provides the result it will send an event
    # based on the name of the respond_to field of the request:
    results LIST;
    RECEIVE response_changed {
        data := ITEM ${response} OF response; # received an array of records
        PUSH ITEMS FROM data TO results;
        LOG "Received response: " + data;
    }
}

