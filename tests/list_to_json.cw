# Example showing how to convert a list of items into a JSON array,
# and then insert that array into a JSON object.
ltoj ListToJson;

ListToJson MACHINE {
    OPTION record JSON_VALUE {
        "key1": 2,
        "key2": "example",
        "values": []
    };
    items LIST "432D432","322D111","123F45Z";
    LOCAL OPTION x 0; # Temporary variable, initial value doesn't matter

    COMMAND convert {
        # using a temporary variable:
        x := ("[" + (SERIALISE items) + "]") AS JSON;
        ITEM ${exclude} OF record := x;

        # or directly without a temporary variable:
        ITEM ${exclude} OF record := ("[" + (SERIALISE items) + "]") AS JSON;
    }
}
