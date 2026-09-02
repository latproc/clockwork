# Handoff: implement QUERY ... INTO <list> in Clockwork (db/language agent)

**For:** the agent owning latproc-db-work (datastore / cw language / cw-scaffold / cw-migrate).
**From:** the Warehouse RECORD conversion (Clockwork side). Offline sim only; nothing plant-side.

> **STATUS: SUPERSEDED.** The auto-populate design proposed below (route the dbsvr reply
> directly into the named LIST) was **not** adopted. Martin's design decisions
> (`docs/RECORD_DB.md` decisions 6, 8, 14 — "Clockwork is the language", "no loops in
> handlers", "Generic `json AS LIST`, not find_all-as-unlinked-machines") closed that path.
>
> What landed instead (all parse/unit tested):
> - `QUERY q INTO list` / `QUERY JSON_VALUE {…} INTO list` parse and SEND the JSON to
>   `DATABASE_CHANNEL`. `QUERY` now injects `respond_to` = `<issuing machine>.response`,
>   so the reply routes back to the querying machine (a `QueryAction`, not the old
>   `(void)$4` `SendMessageAction`). The `INTO <list>` is a *hint*: it names the LIST the
>   reply will become. The scan cannot wait for dbsvr, so `QUERY` itself does **not** fill
>   the list. (PR 7.)
> - `list := reply AS LIST` (or `PUSH ITEMS FROM reply TO list`) turns the returned JSON
>   array into the LIST. (`json AS LIST` = PR 8.) dbd routes the reply's `response`
>   **payload** (the row array, not the `{status,request,response}` envelope) to the
>   `respond_to` target, so `RECEIVE response_changed { list := response AS LIST; }` works.
> - `RECORD APPLY` still maps single-row find/select replies onto RECORD OPTIONS by
>   `(type, key)`; `COPY ALL FROM <Class>` copies held instances.
>
> **Reply routing is now wired and proven.** `QUERY … INTO <list>` routes the multi-row
> `select` reply to the issuing machine's `response` OPTION (array, not envelope); the
> §5 acceptance is met by `RECEIVE response_changed { list := response AS LIST; }`.
> End-to-end coverage: `test_cw_system` drives `QUERY … INTO` and asserts the reply
> reaches `ed.response` as the row array.

## 1. Context - why this matters

Warehouse is moving the HMI panel data layer from HTTP (FastAPI WoolSamplingLineAPI) to
Clockwork RECORD + one shared dbsvr. The panel machines used to call HTTP endpoints that returned
**JSON arrays** (a list of catalog bales, a station queue) and stuffed them into the HMI table.

RECORD today only delivers by **APPLY-by-KEY**: you set a named RECORD's KEY, then find/notify fills
that one row's columns. There is no in-language way to run a *filtered* SELECT (WHERE on a non-KEY
column, ORDER, LIMIT) and get the resulting **rows into a LIST** (thence a JSON array) to feed the HMI.

The intended construct is QUERY ... INTO <list>, but its grammar rule discards the INTO target.
Fixing it unblocks four panel/chamber features, listed in section 5.

## 2. The exact gap (verified in the tree)

iod/src/cwlang.ypp (lines ~1964 and ~1971):

    | QUERY SYMBOL INTO SYMBOL {
        /* JSON on DATABASE_CHANNEL; the reply becomes a LIST via "list := reply AS LIST"
           (or PUSH ITEMS FROM). $4 names that LIST. */
        (void)$4;                                   // <-- INTO target is DISCARDED
        if (current_action) current_actions->push_back(current_action);
        current_action = new SendMessageActionTemplate($2, Value("DATABASE_CHANNEL"));
    }
    | QUERY JSONVAL INTO SYMBOL {
        (void)$4;                                   // <-- same
        if (current_action) current_actions->push_back(current_action);
        current_action = new SendMessageActionTemplate(Value($2), Value("DATABASE_CHANNEL"));
    }

What happens today: the query JSON ($2) is SEND-issued to DATABASE_CHANNEL, and $4 (the LIST the
result is supposed to land in) is ignored. The reply is not routed anywhere the LPC can read it, so
QUERY ... INTO is a no-op on the result.

What is needed: after sending $2 to DATABASE_CHANNEL, capture the dbsvr reply (a JSON array of rows)
and populate the LIST named by $4 - each item a row object (see section 4 for the row shape).

## 3. What already works - do NOT rebuild these

- RECORD classes, TABLE/VIEW, cw-scaffold INTERFACE (find by KEY, create, update, delete,
  load = list-all), cw-migrate.
- dbsvr insert/select/update + view round-trip (verified).
- APPLY-by-KEY: set a named RECORD's KEY, then find/notify APPLYs that one row's columns.
- COPY ALL FROM <RECORD> TO <list> WHERE ITEM.<col> == <literal|OPTION>  (literal OR option).
- SORT <list> BY PROPERTY <col>;  SIZE OF <list>;  TAKE FIRST/LAST FROM <list>.
- reply AS LIST, PUSH ITEMS FROM <json> TO <list> (the JSON-array-to-LIST helpers the grammar
  comment already references).

Do not redo the scaffold find/create/update/delete path - that part is proven and stays.

## 4. What the reply must deliver

QUERY <query-json> INTO <list> should leave <list> holding one item per result row, where each item
is a JSON object keyed by the column names the query asked for, e.g.:

    [
      {"allocated": 0, "bale_id": "MIL001", "bale_no": 1, "desc": "First bale",  "grower_code": "GRW1", "brand": "BrandX"},
      {"allocated": 1, "bale_id": "MIL002", "bale_no": 2, "desc": "Second bale", "grower_code": "GRW2", "brand": "BrandY"}
    ]

The panel then reads these with ITEM ${...} OF row and/or SERIALISE into the HMI table's data.rows
JSON. Column names must match the RECORD OPTION names (CamelCase where the RECORD uses CamelCase,
e.g. BaleNo, LotSize; snake_case where the RECORD uses snake_case, e.g. bale_id, weight_note_wn).

## 5. Concrete acceptance cases

The query JSON is the datastore's select shape (see docs/RECORD_JEMALONG.md):

    {"action":"select","from":"<view/table>","where":{"<col>":"<val>"},"order":["<col>"],"limit":N}

| # | Feature | Query | Must produce |
|---|---|---|---|
| 1 | Catalog dialog table | bales_by_wn WHERE wn = note, ORDER allocated ASC, bale_no ASC | LIST of catalog-bale objects -> HMI table |
| 2 | Next unassigned bale | unassigned_bales ORDER entered_at DESC LIMIT 1 | the next bale_ref to assign a catalog bale |
| 3 | Station occupancy | bale_instances WHERE station = <st> ORDER entered_at ASC | occupancy LIST for the chamber/HMI |
| 4 | Auto KEY pick (chamber move/restart) | bale_instances WHERE station = <st> ORDER entered_at ASC LIMIT 1 | the bale_ref to hand to the dest named RECORD |

Note: the old station_queue view is dropped. Occupancy is bale_instances filtered by station - no
separate view. The "current bale with catalog + weight-note detail" (shortFormDetail) is the
bale_instance_with_links view, which now LEFT JOINs bale_catalog for bale_no/desc/grower_code/brand.

Acceptance for each: a QUERY <json> INTO <list> that leaves <list> readable via ITEM ${...} OF <list>
/ SIZE OF <list> / TAKE FIRST FROM <list>, with a dbd + dbsvr round-trip proof (no FastAPI).

## 6. Files of reference (Warehouse checkout - ~/src/CW_Simulation/Warehouse)

- db/versions/0001_initial.sql - schema + stations + views bale_instance_with_links (with catalog
  join) and rfid_scans_recent. station_queue removed.
- db/versions/0002_catalog_views.sql - unassigned_bales, bales_by_wn.
- lib/db/records.lpc - RECORD classes; lib/db/*INTERFACE.lpc - generated INTERFACEs.
- docs/RECORD_BINDING_NOTE.md - the complete what-works / what-doesn't findings (incl. the
  TAKE FIRST name-string and COPY PROPERTIES literal-lookup limitations).
- docs/RECORD_JEMALONG.md - the datastore select/queue JSON shapes.
- Reference API response shapes (what the panel used to get over HTTP):
  ~/src/github/SamplingLineProjects/WoolSamplingLineAPI/app/api/v1/{notes.py,stations.py,stations_ext.py}
  and app/crud.py get_bales_for_note.

## 7. Scope guard

Only the QUERY ... INTO reply-to-LIST plumbing (grammar + the runtime action that routes the dbsvr
reply into the named LIST) is in scope. Schema/views/INTERFACEs already exist and are correct. This
one change also resolves the long-standing "auto KEY pick" item in the Warehouse plan - it is the
same missing primitive.
