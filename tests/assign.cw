# Play with assignment

X MACHINE {
    OPTION a JSON_VALUE {"a": 1};
    OPTION b JSON_VALUE [1,2,3];
}
x X;

Y MACHINE other {
    OPTION a JSON_VALUE{"a": 1};
    OPTION b 0;
    OPTION c "[1,2,3]";
    OPTION d 0;
    OPTION i 1;
    OPTION j 2;
    list LIST;
    ENTER INIT {
      other := a; # automatic assignment to the properties of another machine (not working yet)
      b := ITEM ${[@i]} OF other.b; # assign a value from a list
      ITEM ${[@j]} OF other.b := 4; # set the value within a JSON array
      d := c AS JSON;               # convert a string to JSON
      PUSH ITEMS FROM d TO list;    # push items from a JSON array to a list
    }
}
y Y x;

Z MACHINE {
   OPTION a JSON_VALUE [3,2,1];
   OPTION b "";
   OPTION c 0;
   rows LIST;

   idle DEFAULT;
   done WHEN SELF IS done;  # prevent cycling

   ready WHEN SELF IS idle AND TIMER > 3000;          # timeout waiting for sampler
   ready WHEN SELF IS idle AND SIZE OF CHANNELS > 0;  # wait for sampler to connect

   ENTER ready{
      b := ITEM ${test[2].value} OF a; #not a valid value, will leave b empty
      IF (CLASS OF b == "EMPTY") {
        c := JSON_VALUE {};
        ITEM ${name} OF c := "Martin";
        #LOG "c: " + c AS STRING;
      }
      ELSE {
        LOG "Error: b should be empty but is: " + CLASS OF b;
      };

      b := ITEM ${test[2].value} OF a DEFAULT 1.0; #not a valid value, will assign default
      IF (CLASS OF b == "FLOAT" AND b == 1.0) {
        LOG "b correctly assigned to default value: " + b AS STRING;
      }
      ELSE {
        LOG "Error: b should be 1.0 but is: " + b AS STRING + " of class " + CLASS OF b;
      };

      b := ITEM ${test[2].value} OF a DEFAULT "a string"; #not a valid value, will leave b empty
      IF (CLASS OF b == "STRING" AND b == "a string") {
          LOG "b correctly assigned to default value: " + b AS STRING;
      }
      ELSE {
          LOG "Error: b should be a default string but is: " + b AS STRING + " of class " + CLASS OF b;
      };

      a := JSON_VALUE {"a": 10, "b": null};
      b := ITEM ${b} OF a DEFAULT 5; # b is null, should assign default
      IF (CLASS OF b == "INTEGER" AND b == 5) {
          LOG "b correctly assigned to default value: " + b AS STRING;
      }
      ELSE {
          LOG "Error: b should be 5 but is: " + b AS STRING + " of class " + CLASS OF b;
      };
      SET SELF TO done;
   }

}
z Z;
