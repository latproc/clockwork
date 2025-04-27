# The following instances should cause the interpreter to haly with errors.

# no_module is not defined
incomplete_subscriber MQTTSUBSCRIBER no_module, "dummy-topic";
incomplete_publisher MQTTPUBLISHER no_module, "dummy-topic", 0;
incomplete_input POINT no_module, "dummy-topic";
incomplete_ouput POINT no_module, "dummy-topic", 0;

# not_a_module is not a MODULE or MQTTBROKER
not_a_module FLAG;
invalid_subscriber MQTTSUBSCRIBER not_a_module, "dummy-topic";
invalid_publisher MQTTPUBLISHER not_a_module, "dummy-topic", 0;
invalid_input POINT not_a_module, "dummy-topic";
invalid_ouput POINT not_a_module, "dummy-topic", 0;
