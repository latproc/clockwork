# Clockwork JSON values

How to create, read, and mutate JSON values in LPC. Source of truth: the JSON path
grammar in `iod/src/json_expr_parser.cpp` and the tests under `tests/json_*.cw`,
`tests/process_json.cw`, `tests/collect_values.cw`, `tests/list_to_json.cw`.

## Literals

`OPTION x JSON_VALUE <json>;` — the value is the JSON text itself:

    OPTION obj JSON_VALUE {"key": "value"};
    OPTION arr JSON_VALUE [1, 2, 3];
    OPTION nul JSON_VALUE null;
    OPTION empty JSON_VALUE {};

## Parse / serialise

- String → JSON:   `x := str AS JSON;`
- JSON → string:   `str := x AS STRING;`

## Reading values — `ITEM ${…} OF x [DEFAULT d]`

The path between `${}` selects an element. The grammar is:

    path   = name { ("[" key "]" | "." field) }
    key    = "*" | name | number
    field  = "@"name | name | numeric_key

| You write | Meaning |
|-----------|---------|
| `a` / `.a` | object key `a` |
| `a.b.c` | nested object keys |
| `[0]` / `[n]` | **array index** `n` |
| `["a"]` | object key `a` (bracket form) |
| `[*]` | first element of an array (wildcard) |
| `@name` | dynamic — value of `name`: integer → array index, string → object key |

Examples:

    x := ITEM ${a} OF obj DEFAULT "";
    x := ITEM ${a.b.c} OF obj;          # nested object
    x := ITEM ${[0]} OF arr;            # array element 0
    x := ITEM ${arr[1]} OF obj;         # obj.arr then index 1
    x := ITEM ${[@i]} OF arr;           # element at the value of i
    x := ITEM ${@key} OF obj;           # key given by the value of key

**Gotcha — dot vs bracket.** A numeric segment after a dot is an **object key**, not an
array index: `.0` looks up the key `"0"`. To index an array use `[0]`.

## Writing values

    ITEM ${key} OF obj := value;     # set / replace an object key
    ITEM ${[i]} OF arr := value;     # set / replace an array element
    ITEM ${@name} OF obj := value;   # dynamic key

## Type inspection — `CLASS OF`

`CLASS OF x` returns one of:

    "JSON_ARRAY"  "JSON_OBJECT"  "INTEGER"  "FLOAT"  "STRING"  "SYMBOL"  "EMPTY"

JSON `null` reads as `EMPTY` (`CLASS OF x IS "EMPTY"`). A non-JSON value assigned into
a JSON option keeps its own class.

## JSON ↔ LIST

- JSON array → LIST:   `list := json AS LIST;`  (clears the list, then pushes each element)
  — equivalent to `CLEAR list; PUSH ITEMS FROM json TO list;`
- LIST → JSON array:   `x := ("[" + (SERIALISE list) + "]") AS JSON;`
- Walk an array of records: `record := TAKE FIRST FROM list;` then read `record`'s keys.

## Worked examples

Parse an MQTT payload and pull fields:

    raw  := tag.message AS JSON;
    ant  := ITEM ${AntId}  OF raw DEFAULT 0;
    t    := ITEM ${RFIDTag} OF raw DEFAULT "";
    rssi := ITEM ${RSSI}    OF raw DEFAULT -999;

Check whether a channel is in a reported `RZs` array (`{"RZs":[1,2]}`):

    rz_json := rz.message AS JSON;
    rz0 := ITEM ${RZs[0]} OF rz_json DEFAULT -1;   # array index 0
    rz1 := ITEM ${RZs[1]} OF rz_json DEFAULT -1;   # array index 1
    IF (rz0 == channel || rz1 == channel) { ...; };

Extract values by a key list into a JSON array (see `tests/collect_values.cw`):

    nested := ITEM ${@key} OF data;
    value  := ITEM ${@key_to_extract} OF nested;
    PUSH value TO extracted;
    result := ("[" + (SERIALISE extracted) + "]") AS JSON;
