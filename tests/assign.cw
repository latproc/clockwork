# Play with assignment

U MACHINE {
    OPTION a JSON_VALUE [{"key": "value1"}, {"key": "value2"}, {"key": "value3"}];
    OPTION current NULL;
    OPTION i 0;

    process_all WHEN SELF IS idle AND CLASS OF current IS NOT EMPTY;
    idle DEFAULT;
    
    ENTER INIT {
        CALL update ON SELF;
    }

    COMMAND update {
        current := ITEM ${[@i]} OF a;
    }

    ENTER process_all {
        CALL process ON SELF;
        INC i;
        CALL update ON SELF;
    }

    COMMAND process {
        LOG "a before: " + a AS STRING;
        current := ITEM ${[@i]} OF a;
        ITEM ${key} OF current := "updated " + (i+1) AS STRING;
        ITEM ${[@i]} OF a := current;
        LOG "a after: " + a AS STRING;
    }
}
u U;

W MACHINE {
OPTION channel 1;
OPTION index "";
OPTION sample JSON_VALUE {
      "id": 5,
      "channel": 1,
      "status": "completed",
      "best_by_antenna": {
        "1": {
          "tag": "3376392D9000006767860903",
          "rssi": -56
        },
        "2": {
          "tag": "3376392D9000006767860904",
          "rssi": -56
        },
        "3": {
          "tag": "3376392D9000006767860905",
          "rssi": -56
        },
        "4": {
          "tag": "3376392D9000006767860906",
          "rssi": -56
        }
      }
    };
    OPTION x "";

    ENTER INIT {
        # ordinary fields
        x := ITEM ${id} OF sample;
        LOG "Sample ID: " + x;
        x := ITEM ${channel} OF sample;
        LOG "Sample Channel: " + x;
        x := ITEM ${status} OF sample;
        LOG "Sample Status: " + x;

        # nested fields with a numeric key

        # using an integer variable for the key after converting to string
        index := channel AS STRING;
        x := ITEM ${best_by_antenna.@index.tag} OF sample;
        LOG "Best by antenna 1 tag: " + x;

        # using an integer variable directly as the key
        x := ITEM ${best_by_antenna.@channel.tag} OF sample;
        LOG "Best by antenna 2 tag: " + x;

        # using a literal numeric key written as a string
        x := ITEM ${best_by_antenna."3".tag} OF sample;
        LOG "Best by antenna 3 tag: " + x;


        # using a literal numeric key
        x := ITEM ${best_by_antenna.4.tag} OF sample;
        LOG "Best by antenna 4 tag: " + x;

        # using a non-existing key with default value
        x := ITEM ${best_by_antenna.5.tag} OF sample DEFAULT "no tag";
        LOG "Best by antenna z tag: " + x;
    }
}
w W;

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
