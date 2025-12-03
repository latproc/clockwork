# Assignment of NULL to machine properties

null_initial NullInitialTest;
null_assignment  NullAssignmentTest;
null_json InitiallyNullJSONTest;
assign_json_null AssignJSONNullTest;

NullInitialTest MACHINE {
  OPTION myProp NULL;

  ok WHEN CLASS OF myProp IS EMPTY;
  error DEFAULT;
}

NullAssignmentTest MACHINE {
  OPTION myProp "";

  ok WHEN CLASS OF myProp IS EMPTY;
  error DEFAULT;

  ENTER INIT {
    myProp := NULL;
  }
}

InitiallyNullJSONTest MACHINE {
  OPTION myProp JSON_VALUE null;

  ok WHEN CLASS OF myProp IS EMPTY;
  error DEFAULT;
}

AssignJSONNullTest MACHINE {
  OPTION a JSON_VALUE {"a": null};
  OPTION ab JSON_VALUE {"a": null, "b": 1};
  OPTION item_a "";
  OPTION item_ab_a "";
  OPTION item_ab_b "";

  ok WHEN CLASS OF item_a IS EMPTY 
      AND CLASS OF item_ab_a IS EMPTY AND item_ab_b == 1;
  error DEFAULT;

  ENTER INIT {
    item_a := ITEM ${a} OF a;
    item_ab_a := ITEM ${a} OF ab;
    item_ab_b := ITEM ${b} OF ab;
  }
}

assign_null_into_json AssignNullIntoJson;

AssignNullIntoJson MACHINE {
  OPTION myJson JSON_VALUE {"key": "value"};
  OPTION key2 "a";
  
  ENTER INIT {
    ITEM ${key} OF myJson := NULL;
    ITEM ${@key2} OF myJson := NULL;
  }
}
