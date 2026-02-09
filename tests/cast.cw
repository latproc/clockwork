# Examples of casts in clockwork

check_string CheckString;

Monitor MACHINE other {
    string WHEN CLASS OF other.check == "STRING";
    unknown DEFAULT;
}

CheckString MACHINE {
     OPTION number 1;
     OPTION string "hello";
     OPTION boolean true;
     OPTION check "";
     OPTION status "";

     monitor Monitor OWNER;

     idle DEFAULT;
     error WHEN SELF IS error OR TIMER > 1000 AND SELF IS running;
     running DURING run {
          check := NULL;
          LOG "check: " + CLASS OF check;
          WAITFOR monitor IS unknown;
          status := "Casting number to string";
          check := number AS STRING;
          WAITFOR monitor IS string;

          check := NULL;
          WAITFOR monitor IS unknown;
          status := "Casting boolean to string";
          check := string AS STRING;
          WAITFOR monitor IS string;

          check := NULL;
          WAITFOR monitor IS unknown;
          status := "Casting check to string";
          check := boolean AS STRING;
          WAITFOR monitor IS string;
     }

     ENTER INIT { WAIT 2000;  SEND run TO SELF; }
     ENTER error { LOG "Error: " + status + " failed"; }
}
