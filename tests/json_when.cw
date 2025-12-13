# Experiment with using WHEN clauses that use JSON values.

WhenTest MACHINE {
   OPTION one JSON_VALUE {"a": 1, "b": 2};

   ok WHEN 1 == ITEM ${a} OF one;
   error DEFAULT;
}
when_test WhenTest;

WhenTest2 MACHINE {
   OPTION one JSON_VALUE {"a": 1, "b": 2};

   ok WHEN ITEM ${a} OF one == 1;
   error DEFAULT;
}
when_test2 WhenTest2;

DelayedWhenTest MACHINE {
   OPTION one JSON_VALUE {"a": 0, "b": 2};

   ok WHEN 1 == ITEM ${a} OF one;
   error DEFAULT;

   COMMAND update {
      ITEM ${a} OF one := 1;
   }
}
delayed_when_test DelayedWhenTest;

Keyring MACHINE {
    OPTION key "a";
}

PropertyWhenTest MACHINE index {
   OPTION one JSON_VALUE {"a": 1, "b": 2};

   ok WHEN 1 == ITEM ${@index.key} OF one;
   error DEFAULT;
}

key_ring Keyring;
property_when_test PropertyWhenTest key_ring;

