# Findings: dbd, batches, and reaching more than one datastore

**Status:** investigation note, no code change. Corrects a report that "a machine can
read N rows but cannot turn them into N rows."

**Scope:** this repo (`iod`/cw + `dbd`) and the datastore repo (`dbsvr`). Generic
`Customer` / `item` shapes only; no application or site names.

## Summary

The datastore already accepts **N rows in a single request**, as an atomic batch.
`dbd` is the component that cannot carry a batch: it assumes one request object per
message. The "cannot write 25 rows" outcome is therefore a `dbd` bridge-shape gap,
not a missing datastore feature and not (primarily) a missing language loop.

## What the datastore already supports

A request body may be a **JSON array of request objects** (a batch). It runs as one
ordered, atomic transaction on one worker connection; the reply is a JSON array of
per-query envelopes in the same order. If any entry fails, the whole batch rolls back.

- `../datastore/README.md` — "Requests", "Responses" (batch array)
- `../datastore/dbsvr.cpp` — `performAnyMessage` routes an object to the single-query
  path and an array to the ordered-atomic-batch path
- `../datastore/tests/test_batch.cpp` — CI test (`add_test(NAME test_batch …)`),
  asserts a `find` inside a batch sees an earlier `insert` in the same batch

Example (25 rows = 25 insert entries, one message):

```json
[
  {"action":"insert","auth":"…","type":"item","data":{"id":1}},
  {"action":"insert","auth":"…","type":"item","data":{"id":2}}
]
```

So `{"action":"insert","data":[…]}` is rejected (`data` must be an object —
`dbmock/sql_interface.cpp`, `collectObjectBinds` → `invalid JSON object`), but that
is **not** the bulk path. The bulk path is the top-level array.

`action:"sql"` is rejected by design (`sql_interface.cpp`; README: "use `cw-migrate`
or the sqlite CLI"). This is intentional, not a defect.

## What `dbd` does today

`iod/src/dbd.cpp` handles exactly one request object per message:

- `send_response_to_clockwork()` reads `respond_to` off the **request** and `response`
  off the **reply envelope** as objects. Given an array, `cJSON_GetObjectItem(array,
  "respond_to")` is null, so it falls back to the default `manager.response`, and
  `cJSON_GetObjectItem(arrayReply, "response")` is null, so it forwards the whole
  array of envelopes rather than the per-entry payloads.
- `apply_rows_to_records()` reads `type` / `action` / `keys` off the request. On an
  array `type` is null, so it returns early — no `RECORD APPLY` for any entry.

Net effect: a batch forwarded through `dbd` **commits** at `dbsvr`, but its replies
are mis-routed and no RECORD projection happens. Neither function is array-aware.

## Reaching more than one datastore

Two hardcoded names constrain a single cw to one datastore:

- `QueryAction::run()` sends every `QUERY` to `DATABASE_CHANNEL`
  (`iod/src/QueryAction.cpp`).
- `dbd` subscribes to `DATABASE_CHANNEL` with no override
  (`iod/src/dbd.cpp`, `SubscriptionManager subscription_manager("DATABASE_CHANNEL", …)`).

`SEND` is *not* limited to a literal: `SEND SYMBOL TO SYMBOL` resolves a symbol that
holds a `JSON_VALUE` OPTION and transmits the full JSON text
(`SendMessageAction::run()`), which is exactly how the generated `<Class>INTERFACE`
sends `insert` / `update`. So a second datastore is reachable by `SEND` to a
differently-named channel — once `dbd` can subscribe to a channel other than
`DATABASE_CHANNEL`. `QUERY` would still target `DATABASE_CHANNEL` until it can name a
channel too.

## Padded / leading-space key investigation

Claim under test: "leading spaces are stripped from a numeric-looking string as it
passes through a property, so a padded equality key cannot be sent from Clockwork."

**Result: the general claim is false; a specific scalar-property path does strip it.**

In-language JSON values preserve padding. A probe program (`cw` with a `JSON_VALUE`
holding `"  267968"`) shows leading spaces surviving:

- `JSON_VALUE` literal → `{"recno":"  267968","n":25}` (unchanged)
- `STRING` literal → `[  267968]`
- `ITEM ${a} OF src` → `CLASS OF` = `STRING`, value `  267968`
- `ITEM ${b} OF dst := <that item>` → `{"b":"  267968"}`
- `ITEM ${c} OF dst := "  267968"` → `{"b":"  267968","c":"  267968"}`

The lexer (`cwlang.lpp`, `JSONSTR`/`STRING` states), `assign_value` (`value.cpp`) and
`MessageEncoding` (cJSON-based) all preserve the bytes. So a padded equality key
**can** be built and sent from a Clockwork program, e.g. as a `JSON_VALUE` query.

The real defect is narrower: `IODCommandProperty::run` (`iod/src/IODCommands.cpp`)
decides whether an incoming scalar `STRING`/`SYMBOL` is an integer with
`strtol(...)` followed by `*p == 0`. `strtol` skips **leading whitespace**, so
`"  267968"` is judged an integer and stored as `267968` — the padding is lost:

    input="267968"    strtol=267968  rest=""   -> is_integer=1
    input="  267968"  strtol=267968  rest=""   -> is_integer=1   <-- padding lost
    input=" 267968 "  strtol=267968  rest=" "  -> is_integer=0   (preserved)

This affects only a scalar value set through the external `PROPERTY` command (HMI /
inter-process property write), not JSON values and not a JSON array reply. A JSON
reply routed by `dbd` carries the payload as a `JSON` value, whose inner strings are
untouched.

Candidate one-line fix: require the first character to be a sign or digit before
accepting the integer classification, e.g.

    const std::string s = params[3].asString();
    const bool is_integer = !s.empty() && *p == 0 &&
                            (s[0] == '-' || s[0] == '+' || isdigit((unsigned char)s[0]));

That preserves `"  267968"` as a string while leaving `267968` / `-42` as integers.

## The loop question

"No loops in handlers" and "no embedded Lua/Python" are deliberate design decisions
(`RECORD_DB.md`, key decision 14 and the non-goals), not defects. The sanctioned shape
for a query result is: JSON array → `list := reply AS LIST` → drain onto a **named**
RECORD with `TAKE FIRST` / `WAITFOR` / `COPY PROPERTIES`.

Consequence for a row-by-row *transform* (source rows → differently-shaped insert
requests): that mapping needs iteration, which the language deliberately does not
provide. The right place for such fan-out is outside LPC — at the datastore/Store
boundary, the bridge, or an external driver issuing a batch — rather than in a
handler loop.

## Recommended fix order

1. ~~Correct the record (this note).~~ **Done.**
2. ~~Investigate the padded-key claim with a targeted test.~~ **Done** — see above.
   The `IODCommandProperty` scalar coercion is a separate, small bug; fix optional.
3. ~~`dbd --channel <name>` so one cw can reach a second datastore.~~ **Landed.**
   `dbd` takes `--channel <name>` (default `DATABASE_CHANNEL`) and subscribes to it.
4. ~~Make `dbd` batch-aware.~~ **Landed.** `dbd` now fans a top-level array request
   and its array reply out per entry (`send_one_response_to_clockwork` /
   `apply_one_to_records`), honouring each entry's own `respond_to`.

Regression coverage: `iod/tests/test_dbd_batch.cpp` runs one cw (two channels), one
`dbsvr` and two `dbd` (default channel and `--channel ALT_CHANNEL`). It asserts a
two-insert batch commits both rows, a two-select batch routes `Ann` and `Bob` to two
different `respond_to` properties, and a request on `ALT_CHANNEL` is delivered.
Verified to fail (exit 7, batch fan-out) before the `dbd` change and pass after.

Still open (not in this slice): `QUERY` in Clockwork can only target
`DATABASE_CHANNEL`; a second datastore is reachable today via `SEND … TO <channel>`.

## Non-issues (verified)

- Reading N rows into a LIST works and is covered: `QUERY … INTO` injects
  `respond_to`; `dbd` routes the `response` payload (row array); the author assigns it
  with `list := response AS LIST`. See `iod/tests/test_cw_system.cpp` and
  `docs/JSON.md`.
- `LIKE` is a supported bound `where` operator (`RECORD_DB.md`), so a fixed-width
  padded key can be matched without an equality bind.
