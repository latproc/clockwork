# Demonstration of SERIALISE and DESERIALISE commands

digits LIST 0,1,2,3,4,5,6,7,8,9;
Flags LIST f1, f2, f3;
f1 FLAG(name: "flag 1");
f2 FLAG(name: "flag 2");
f3 FLAG(name: "flag 3");

value1 VARIABLE "value1";
value2 VARIABLE 2; 
ValueList LIST value1, value2;

SerialiseDemo MACHINE list_of_machines, list_of_numbers {

  my_list LIST;
  OPTION cache "";
  ENTER INIT {
    CLEAR my_list;
    COPY ALL FROM list_of_machines TO my_list;
    LOG "Flag names: " + (SERIALISE name FROM list_of_machines SEPARATED BY ",");
    LOG "numbers: " + SERIALISE list_of_numbers SEPARATED BY " ";
    LOG "states: " + SERIALISE STATE FROM list_of_machines SEPARATED BY ";";
    LOG "enabled: " + SERIALISE ENABLED FROM list_of_machines SEPARATED BY ";";
    LOG "VALUE if found, otherwise state: " + SERIALISE list_of_machines;

    SEND turnOn TO list_of_machines;
    cache := SERIALISE list_of_machine; # cache <- "on,on,on"
    DESERIALISE STATE SEPARATED BY "," FROM cache TO ITEMS IN list_of_machines;
    SEND turnOff TO list_of_machines;
    cache := SERIALISE list_of_machine; # cache <- "off,off,off"
    DESERIALISE cache TO ITEMS IN list_of_machines;
  }

}
serialise SerialiseDemo Flags, digits;

SerialiseValues MACHINE values {
  my_values LIST;
  OPTION cache "";
  ENTER INIT {
    CLEAR my_values;
    COPY ALL FROM values TO my_values;
    LOG "Values: " + SERIALISE my_values SEPARATED BY ",";
    cache := SERIALISE my_values SEPARATED BY "|";
    value1 := "new_value1";
    value2 := 42;
    LOG "after updates: " + SERIALISE my_values SEPARATED BY ",";
    DESERIALISE cache SEPARATED BY "|" TO ITEMS IN my_values;
    LOG "Deserialised Values: " + SERIALISE my_values SEPARATED BY ",";
  }
}
serialise_values SerialiseValues ValueList;
