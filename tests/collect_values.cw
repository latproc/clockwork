# Demonstration of extracting specific result from items nested
# in a JSON structure using a list of keys.
#
# Set the list of keys to extract then SEND extractor.extract to start the process.
# When extractor IS done, the result will be a JSON array of the extracted values.

keys_to_extract LIST "a","c";
extractor Extractor keys_to_extract, data;

data CONSTANT JSON_VALUE {
        "a": {"key1": "tag_a", "key2": "value2"},
        "b": {"key1": "tag_b", "key2": "value4"},
        "c": {"key1": "tag_c", "key2": "value6"}
    };

Extractor MACHINE keys, data{
    OPTION key_to_extract "key1";
    OPTION result JSON_VALUE [];

    # using a temporary work list, build a list of extracted valiues
    # and then serialise the list to a JSON array as the final result
    work LIST;
    extracted LIST;
    LOCAL OPTION nested JSON_VALUE {};
    LOCAL OPTION key "";
    LOCAL OPTION value "";

    idle DEFAULT;
    done WHEN SELF IS done OR SELF IS serialise;
    serialise WHEN SELF IS working AND work IS empty;
    working WHEN SELF IS get_next;
    get_next WHEN work IS NOT empty;

    COMMAND reset {
        result := JSON_VALUE [];
        CLEAR extracted;
        SET SELF TO idle;
    }

    COMMAND extract { COPY ALL FROM keys TO work; }

    COMMAND next { key  := TAKE FIRST FROM work; }

    ENTER get_next { CALL next ON SELF; }

    ENTER working {
        nested := ITEM ${@key} OF data;
        value := ITEM ${@key_to_extract} OF nested;
        nested := JSON_VALUE {};
        PUSH value TO extracted;
    }

    ENTER done {
        key := "";
        value := "";
        result := ("[" + (SERIALISE extracted) + "]") AS JSON;
    }
}
