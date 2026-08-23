# Clockwork RECORD and native database

**Status:** Draft (amended after original-author comments; more review still coming)  
**Date:** 2026-08-23  
**Author:** (design)  
**Repos:**

| Repo | Path | Role |
| --- | --- | --- |
| Clockwork / iod | this repo (`latproc`) | Language, MACHINE/RECORD, `dbd` as DATABASE_CHANNEL adapter |
| datastore | `../datastore` (`github.com/latproc/datastore`) | JSON database server (`dbsvr` on ZMQ `tcp://*:5554`), pluggable `Store` |
| JemalongDB | `WarehouseSIM/JemalongDB` | LPC client of DATABASE_CHANNEL |
| Warehouse | plant HTTP client | `WEBREQUEST` to SamplingLine APIs |
| SamplingLineProjects | Python + Alembic | operational warehouse data today |

This is the design spec only. Language, `dbd`, datastore, Warehouse LPC, SamplingLine, and `cw-migrate` implementation happen later on a separate branch.

---

## Overview

Clockwork already has a working sketch of database access, built under limited time: JSON action messages on `DATABASE_CHANNEL`, a `dbd` daemon that forwards them, and the **datastore** process that actually talks to a database. JemalongDB used that path for weight notes and bale details. Warehouse, needing a live plant data path before the Clockwork side was finished, talks HTTP (`WEBREQUEST`) to WoolSamplingLineAPI and copies JSON into `BALEDETAILAPI` OPTIONS, with `ChangeCounter` so panels know when to refresh.

Those pieces show the intended model clearly: a row should look like a MACHINE, LIST/HMI should work with it, and SQL (or any other backend) stays **out of iod**. What remains is to finish that model so OPTIONS *are* the row, so a commit can reach every Clockwork that holds that RECORD, and so schema (including views) is versioned like Alembic.

The next step is a **`RECORD` class** (a parser-restricted MACHINE) plus the **existing datastore server**, not a new SQL engine inside `dbd`:

- RECORD is MACHINE with a limitation: no user handlers or states. Lexer/parser stop people adding logic; OPTION-change and dependency tracking already on MACHINE then just work.
- `dbd` stays the Clockwork adapter: subscribe as `DATABASE_CHANNEL`, forward JSON, apply replies onto machines.
- **datastore** (`dbsvr`) is the database process. JSON in, JSON out. `SQLInterface` compiles JSON to SQL for the current sqlite `Store`. Other backends (redis, later others) are a Store swap — that was the point of keeping it separate.
- Clockwork queries stay JSON (Jemalong `action` / `keys` / `fields`). Joins and views are datastore + migrations, not SQL strings inside WHEN.
- LIST operations work because RECORD instances are ordinary `MachineInstance`s.

---

## Background and motivation

### What exists today

| Piece | Location | What it actually does |
| --- | --- | --- |
| Language sketch | `tests/datastore.cw`, `tests/db-channel.cw` | Customer MACHINE with JSON action templates; `SEND request TO DATABASE_CHANNEL`; LOOKUP and result mapping still marked as future work |
| Jemalong client | `/Users/mike/src/latproc/WarehouseSIM/JemalongDB/` | Weight note + bale details as MACHINEs + INTERFACE helpers + `jemalong.conf` / `jemalong.db` |
| Channel | `database_channel.lpc`, `db-channel.cw` | `DATABASE_CHANNEL` PUBLISHER; `IGNORES STATE_CHANGES, PROPERTY_CHANGES` — only `SEND` JSON moves |
| Clockwork daemon | `iod` `dbd` | Subscribes as `DATABASE_CHANNEL`, forwards payload to `tcp://127.0.0.1:5554`, then `PROPERTY` a JSON blob and `SEND <prop>_changed` |
| Database server | `../datastore` (`dbsvr`) | JSON request/reply on ZMQ REP `:5554`. `SQLInterface::buildSQL` → `Store` (sqlite3 today) |
| Persistence (unrelated) | `PersistentStore`, `persistd` | Key/value dump of properties to `persist.dat`. Not a database. |

`dbd` `send_response_to_clockwork` parses `respond_to` as `machine.property`, sets that property to the JSON response, then sends `response_changed`. That was enough to prove the channel path. RECORD continues from there by applying the same notify path **per column** onto the row MACHINE, so OPTIONS stay in sync. Port 5554 is not a throwaway helper: it is **datastore**.

### datastore (`../datastore`)

Separate project (`git@github.com:latproc/datastore.git`). README: *“A basic data store, using sqlite3 initially, will also eventually support redis and perhaps other databases.”* ZMQ for JSON requests and JSON replies.

`dbsvr` (`dbsvr.cpp`):

- Loads `db_name` from `--config` (same idea as Jemalong `jemalong.conf`).
- `Store::getInstance(db_name)->connect(true)`.
- Binds `tcp://*:5554` as ZMQ REP.
- `handleIncomingRequest` → `performRequestMessage`.

JSON protocol (already what Jemalong sends):

```
{ "action": "find", "auth": "...", "type": "customer", "keys": { "name": "Fred" }, "fields": ["age"] }
{ "action": "insert", "auth": "...", "type": "customer", "data": { "name": "Fred" } }
{ "action": "update", "auth": "...", "type": "customer", "keys": { "name": "Fred" }, "data": {"age":20} }
{ "action": "delete", "auth": "...", "type": "customer", "keys": { "name": "Bill" } }
{ "action": "create", "auth": "...", "type": "customer", "schema": { "name": "string primary key" } }
{ "action": "sql", "auth": "...", "sql": "…" }
```

Replies: `{ "status": 0|1|2, "request": "…", "response": … }`. Auth token placeholder is `"xxx"` (restrictions intended later).

`SQLInterface` turns `action`/`type`/`keys`/`data`/`fields`/`schema` into SQL. `Store` currently wraps sqlite3 (`prepare`/`step`/`finalize`). A Redis (or other) Store would keep the JSON API and leave Clockwork unchanged.

That is why sqlite should **not** move into `dbd`. Folding the engine into the Clockwork daemon would work for one backend and then fight the intended split.

### JemalongDB: what the current features look like in use

Files:

- `WeightNote.lpc` — `WEIGHTNOTE MACHINE` with row OPTIONS (`wn`, `bales`, `cores`, `grabs`) **and** JSON templates (`create`/`insert`/`find_by_wn`/`find_all`/`update`/`delete`).
- `BaleDetails.lpc` — same pattern for catalog-ish columns (`baleId`, `eBaleId`, `weight`, …).
- `database.lpc` — one instance plus one INTERFACE per type; `NULL CONSTANT ""` as a stand-in while native NULL support was still landing.
- `database_channel.lpc` — publisher on port 10708, `MONITORS \`.*\``, ignores property/state changes.
- `jemalong.conf` — `db_name jemalong.db`.

This is a solid use of the features that existed: the row MACHINE holds fields, the INTERFACE builds the JSON the channel expects, and `respond_to` is how results come back.

1. HMI or program writes OPTIONS on the row MACHINE.
2. INTERFACE copies OPTIONS into `request` JSON (`ITEM ${data.wn} OF request := weight_note.wn`).
3. `SEND request TO DATABASE_CHANNEL`.
4. `dbd` forwards to datastore; the reply comes back via `respond_to`.
5. INTERFACE receives `response` / `response_changed` and stores JSON in `data`.

What we still want from the runtime, which this layer could not provide yet:

- Apply the result onto the **same OPTIONS** that were sent, so a second read is unnecessary.
- `find_all` as a LIST of row machines, not a JSON array on one property.
- Schema as versioned migrations rather than a runtime `create` JSON payload (`"wn": "string primary key"`).
- One receive-message name for results (`dbd` sends `response_changed`; the two INTERFACEs currently listen for slightly different messages).
- Field copies that stay aligned with `data.*` vs `keys.*` without a hand-maintained mapper.

RECORD is the language support that makes the row MACHINE the schema. The INTERFACE + JSON path stays valid; it is the proven persist path until something more is specified.

### Warehouse HTTP path (SamplingLine APIs)

`/Users/mike/src/latproc/Warehouse/lib/api/samplingline_api.lpc` is the plant path that shipped: `WEBREQUEST` machines (`BALECREATEOBJ`, `BALERETRIEVEOBJ`, …) talk to FastAPI. `ENTER done` copies JSON keys into `BALEDETAILAPI` OPTIONS (`lib/BaleObject.lpc` — already “a row as OPTIONS”). Panels use `V_StationJSONChangeCounter` and `localChangeCounter != ChangeCounter.VALUE` (`machine/Panel.lpc`) to refresh when something may have changed.

WoolSamplingLineAPI (`SamplingLineProjects/WoolSamplingLineAPI`) is Alembic-managed SQLite (`BaleInstance`, stations, weight notes). Responses are **joined documents**, not single tables: `bale_with_links_dict` attaches the current catalog version and current weight-note version (`app/utils.py`). Station queues, `/api/v1/stations/bales`, reports, and v2 occupancy endpoints (`specs/02-api-contract.md`, `app/api/v2/stations.py`) are the same idea — SQLAlchemy `.join` / window-style ranking, presented as JSON. There are no `CREATE VIEW` objects in `docs/schema.sql` today; the “views” are API-shaped joins. `warehouse-status-site` polls `/api/v1/stations/{station}/queue`.

Two Clockwork iods (Grab and Core Warehouse configs) both use this HTTP API as shared memory. `BALEREFMOVE` on one machine plus `ChangeCounter` on the other is how a bale leaving Grab appears on Core.

RECORD + one shared **datastore** is how Clockwork can take on that job: a write on one iod commits once in `dbsvr`; both Clockworks see the OPTIONS change without an HTTP GET. How the second iod is notified is still open (datastore is request/reply today) — see Open questions.

---

## Goals and non-goals

### Goals

1. **`RECORD` class** — MACHINE with no user handlers or states. OPTIONS are columns of a table **or a view**. Parser (lex/bison) enforces the limitation. Existing OPTION-change logic updates dependents.
2. **Row OPTIONS stay in sync** on Clockworks that hold the row, so a write on Grab is visible on Core without HTTP GET or ChangeCounter.
3. **JSON in Clockwork; backend ops in datastore.** Extend the existing `action`/`type`/`keys`/`fields` protocol (joins, views, filters). Named views in migrations stand in for today’s API flatten (`bale_instance_with_links`, station queue).
4. **Alembic-like schema management** for tables *and* views: versioned upgrade/downgrade, revision table, CLI, no silent prod auto-mutate.
5. **LIST commands work on RECORD instances** (`COPY`, `TAKE FIRST`, `SORT BY PROPERTY`, `CLEAR`, `SIZE OF`, ALL/ANY, SUM/MIN/MAX, `SEND TO list`).
6. **Non-blocking iod scan:** iod never opens the database file. `dbd` does not become the SQL engine.
7. Design now; implement later on a new branch.

### Non-goals (this design)

- Replacing WoolSamplingLineAPI or warehouse-status-site in the first PRs.
- Making `PersistentStore` / `persistd` a SQL database.
- SQL strings inside WHEN clauses.
- Merging sqlite into `dbd` (monolithic; fights the Store split).
- A new `MachineInstance` subclass or second process loop for RECORD.
- Reimplementing SQL in the Clockwork parser.
- Automatic rename detection in migrations.
- Embedding a Python ORM in iod.

---

## Proposed design

### Architecture

```mermaid
flowchart LR
  subgraph grab [Grab Clockwork]
    RecG[RECORD OPTIONS]
    LstG[LIST]
    DbdG[dbd]
  end
  subgraph core [Core Clockwork]
    RecC[RECORD OPTIONS]
    LstC[LIST]
    DbdC[dbd]
  end
  subgraph ds [datastore]
    Dbsvr[dbsvr :5554]
    Store[Store sqlite now]
  end
  RecG -->|JSON SEND| DbdG
  RecC -->|JSON SEND| DbdC
  DbdG -->|ZMQ JSON| Dbsvr
  DbdC -->|ZMQ JSON| Dbsvr
  Dbsvr --> Store
  DbdG -->|PROPERTY columns| RecG
  DbdC -->|PROPERTY columns| RecC
  DbdG -->|LIST membership| LstG
  DbdC -->|LIST membership| LstC
```

Keep the three roles:

| Process | Job |
| --- | --- |
| **iod** | RECORD/MACHINE instances, LIST, WHEN on *other* machines, HMI |
| **dbd** | DATABASE_CHANNEL adapter. Forward JSON to `:5554`. Apply reply as PROPERTY (today a blob; next, per column onto OPTIONS) |
| **datastore (`dbsvr`)** | One writer. JSON → Store. sqlite today; redis and others later without changing Clockwork |

Grab and Core must **not** each open the database file. Two writers would split the RECORD story. **One `dbsvr`** is the shared process both plant iods already need.

`dbd` stays out of iod for the same reason as `persistd` / `modbusd`: a backend lock must not stall EtherCAT. Putting sqlite *inside* `dbd` would work for the current Store and then make a Redis (or second) backend a Clockwork change. Leave Store in datastore.

Connection: named database from config (Jemalong `jemalong.conf` `db_name`). Datastore already reads that. CHANNEL `KEY` as today. Network: `dbsvr` on the warehouse LAN so both dbds can reach `:5554`.

**Fan-out (open):** datastore is ZMQ REP today — one request, one reply. A SAVE on Grab updates that iod after the reply. Core does not hear about it unless (a) datastore grows a publish/notify after commit, (b) Core’s dbd is told to refresh, or (c) something else Martin specifies. That is **not** a reason to merge sqlite into `dbd`.

### RECORD is MACHINE with a parser limit

FLAG, LIST, VARIABLE are already `MachineClass` plus ordinary `MachineInstance`. PROPERTY changes already call `notifyDependents()`, so WHEN on **other** machines re-evaluates. That is the auto-update path.

RECORD is the right name. Implementation is a **simple lex/bison change** that stops people adding logic. After that, OPTION-change logic should just work. Do **not** add a second internal machine type.

Parser today (`cwlang.ypp`): `SYMBOL STATEMACHINE` is `Name MACHINE { ... }` and creates `new MachineClass($1)`. Builtin FLAG/LIST are also `MachineClass` (`clockwork.cpp`). `MachineInstanceFactory::create` special-cases a few types; everything else is a normal `MachineInstance`.

Add:

- Lexer token `RECORD` (like `MACHINE` / `FLAG`).
- `definition_header: SYMBOL RECORD …` creating a `MachineClass` with `is_record = true` (a flag is enough; optional `token_id`). Table name derived from class name.
- Instances are **normal** `MachineInstance`s. LIST, EXPORT, HMI, and dependency tracking apply unchanged.
- **Reject in a RECORD body:** WHEN, RECEIVE, COMMAND, user states, transitions. OPTIONS only (`KEY` / `LOCAL` / `UNIQUE` / `NOT NULL` as agreed).
- Optional `VIEW "name"` (or `TABLE "name"`) on the class so a RECORD can sit on a join view, not only a base table.

Sketch (JemalongDB rewritten):

```
WEIGHTNOTE RECORD {
    OPTION wn "" KEY;
    OPTION bales 0;
    OPTION cores 0;
    OPTION grabs 0;
}

note WEIGHTNOTE (wn: "C0000");
```

Persist stays the proven path until a later comment specifies builtins: INTERFACE + JSON templates + `SEND … TO DATABASE_CHANNEL`. Logic that reacts to the row lives on a **MACHINE** that depends on the RECORD:

```
NoteEditor MACHINE note {
    COMMAND create_or_save {
        SEND insert TO DATABASE_CHANNEL;
    }
    COMMAND lookup {
        SEND find_by_wn TO DATABASE_CHANNEL;
    }
}
```

If save/load later become builtins, follow FLAG: FLAG has `turnOn`/`turnOff` as class transitions, not user WHEN. Do not put `COMMAND save { SAVE SELF }` or `dirty WHEN SELF IS changed` in the RECORD body.

`BALEDETAILS` similarly becomes a RECORD whose OPTIONS are exactly the current row fields (`baleId` KEY, `eBaleId`, `weight`, …). That is the same shape as `BALEDETAILAPI` in Warehouse.

#### OPTIONS = columns

- Persisted OPTIONS: ordinary `OPTION name default`. Default and Clockwork type (`integer`/`string`/`float`/`boolean`/NULL) map through datastore (sqlite: `INTEGER`/`TEXT`/`REAL`/`INTEGER 0/1`/`NULL`; other Stores map their own types).
- `LOCAL OPTION` is **not** a column (ephemeral, same as today).
- `OPTION PERSISTENT` on a RECORD class is ignored or illegal: the table (or Store) is the persistence.
- JSON_VALUE OPTIONS are allowed as text storing JSON (escape hatch, not the primary row model).
- `KEY` / `UNIQUE` / `NOT NULL` annotations on OPTION (new grammar). First `KEY` is the primary key. Composite keys: later if needed; v1 is single-column KEY.
- `NULL` is a real Value, so the `NULL CONSTANT ""` stand-in in `database.lpc` is no longer needed.

Identity:

- **Named instance** (`note WEIGHTNOTE (wn: "C0000")`) is a bound working row.
- **Registry:** `(database, table, primary_key) → MachineInstance*`. `find_all` / COPY into a LIST **reuses** the instance for a PK. Never two machines for one row.
- **Anonymous/dynamic instances** created by queries get a stable internal name, e.g. `WEIGHTNOTE#C0000`.

dbd applying per-column PROPERTY is what makes OPTIONS the row. Machines that depend on those OPTIONS already re-check. The author does **not** issue a second FIND for that.

### Writes and “no extra calls”

**Local writes (either Clockwork is the writer):**

1. Program or HMI assigns OPTIONS (`note.bales := 12`). **No database yet.**
2. An explicit persist (INTERFACE `insert`/`update`, or a later builtin) sends JSON on DATABASE_CHANNEL.
3. `dbd` forwards to datastore. `dbsvr` commits in Store.
4. Reply maps onto OPTIONS (per column). `notifyDependents()` runs. There is **no** follow-up GET on that iod.

Implicit write-through on every OPTION assignment is rejected: HMI fills several fields; WHEN on dependents would fire mid-edit. Explicit persist matches Jemalong COMMAND `insert`/`update`.

**Inbound PROPERTY path:** iod command thread (`IODCommands` PROPERTY). Applying a datastore reply must not bounce back as another persist.

**Python / website during coexistence:** apply through the same JSON API (`dbsvr`) so Clockwork clients can see the write. If Python writes the sqlite file directly, notification is the same open fan-out question.

### JSON queries and SQL views

WoolSamplingLineAPI does not store a denormalized bale row. `GET /bales/{ref}` and station queues return **joins**: `bale_instances` plus current `bale_catalog_versions` and current `weight_note_versions` (`bale_with_links_dict`). Clockwork needs the same shapes without putting SQL in WHEN.

**Where SQL lives:** datastore (`SQLInterface` + Store) and migration files. sqlite already has `JOIN`, `LEFT JOIN`, `CREATE VIEW`, subqueries, window functions. A non-SQL Store implements the same JSON actions in its own way.

**Where Clockwork speaks:** JSON, extending Jemalong’s `action` / `type` / `keys` / `fields`:

```
OPTION queue_query JSON_VALUE {
  "action": "select",
  "from": "bale_instance_with_links",
  "where": { "station": "GrabChamber" },
  "order": ["bale_no"],
  "limit": 20
};
```

Ad-hoc join (when a named view does not exist yet):

```
{
  "action": "select",
  "from": { "table": "bale_instances", "as": "bi" },
  "join": [
    { "type": "left", "table": "stations", "as": "st",
      "on": [["bi.station_id", "st.id"]] },
    { "type": "left", "table": "weight_notes", "as": "wn",
      "on": [["bi.weight_note_id", "wn.id"]] }
  ],
  "select": {
    "bale_ref": "bi.bale_ref",
    "station": "st.name",
    "wn": "wn.wn"
  },
  "where": { "st.name": "GrabChamber" },
  "order": ["bi.bale_no"]
}
```

datastore compiles this to parameterized SQL (identifiers from a catalog allow-list; values bound). No string-concatenated SQL from LPC. `action: "sql"` already exists as an operator/debug hatch; it is not the program default.

**Named views** (preferred for plant queries). Example matching the API flatten:

```
CREATE VIEW bale_instance_with_links AS
SELECT bi.bale_ref AS bale_ref,
       st.name AS station,
       wn.wn AS wn,
       bi.rfid_tag AS rfid_tag,
       bi.updated_at AS updated_at
FROM bale_instances bi
LEFT JOIN stations st ON st.id = bi.station_id
LEFT JOIN weight_notes wn ON wn.id = bi.weight_note_id
…
```

A RECORD can bind to that view:

```
BaleWithLinks RECORD VIEW "bale_instance_with_links" {
    OPTION bale_ref "" KEY;
    OPTION station "";
    OPTION wn "";
    OPTION rfid_tag NULL;
}
```

`COPY ALL FROM BaleWithLinks TO grab_queue WHERE …` then works. Writes still go to the base table RECORD (or a documented INSTEAD OF trigger later).

**QUERY** (if added on a MACHINE, not on the RECORD): `QUERY <json> INTO <list>` and `QUERY <json> INTO <record>`. JSON in CW, SQL in datastore.

### LIST integration

LIST members are `MachineInstance*` (`SetOperationAction`, `PopListAction`, `SortListAction`, `IncludeAction`, `UpdateListItemsAction`, `dynamic_value` SUM/MIN/MAX). RECORD instances participate with **no LIST core changes** once they exist as machines.

What is missing is **materializing a query into a LIST**. Today `COPY ALL FROM src TO dst WHERE predicate` (`cwlang.ypp`) requires `src` to be a LIST.

Extend COPY (and only COPY, v1) so `src` may be a **RECORD class name** (virtual table):

```
weight_notes LIST;

NoteEditor MACHINE {
    COMMAND refresh {
        CLEAR weight_notes;
        COPY ALL FROM WEIGHTNOTE TO weight_notes;
    }
    COMMAND heavy {
        COPY ALL FROM WEIGHTNOTE TO weight_notes WHERE WEIGHTNOTE.ITEM.bales > 10;
    }
}
```

`ITEM` already means “the member being considered” in LIST WHERE clauses (`tests/copy.cw`). For a RECORD class source, datastore runs `find`/`select` against the table **or view**. Simple `WHERE` on COPY still maps to JSON `where`. Joins belong in the view or in a `QUERY` JSON `join` list, not in WHEN.

`TAKE FIRST FROM weight_notes` then works unchanged.

**Query subscriptions (the queue case)** stay a later piece of the fan-out question: Warehouse station queues need “this LIST stays the station’s rows” without re-querying every scan cycle.

Instance reuse: COPY INTO a LIST does not destroy a RECORD that is also a named instance or a member of another LIST.

### Completing `dbd` and datastore (keep the split)

Today `dbd` parses JSON, `client.connect("tcp://127.0.0.1:5554")`, `makeRemoteRequest`, dumps a blob back. That is the right shape.

**dbd next:**

- Keep forwarding Jemalong `create`/`insert`/`find`/`update`/`delete` JSON to datastore.
- Map a successful row (or rows) onto OPTIONS per column, not only `respond_to` as one JSON property.
- Skip-dirty / no echo persist on inbound PROPERTY.

**datastore next:**

- Parameterized SQL (today `SQLInterface` concatenates; tighten with an identifier allow-list and bound values).
- Richer JSON: `select` / `join` / `view` / `order` / `limit`.
- Store remains sqlite3 until a Redis (or other) Store is written. Clockwork does not care.
- Optional notify-after-commit if that is how two Clockworks share a row (open).

Do **not** open sqlite from `dbd`. Do **not** fold `dbsvr` into the Clockwork tree as “the SQL worker”.

### Alembic-like schema (`cw-migrate`)

RECORD classes in `.cw`/`.lpc` are the **table** model. **Views** are first-class migration objects (`CREATE VIEW` SQL), optionally bound to a RECORD with `VIEW "name"`.

Tool name: `cw-migrate`. Lives next to **datastore** (or a small tool that speaks JSON `create` / later migration actions), not as a sqlite helper inside `dbd`.

| Command | Behaviour |
| --- | --- |
| `cw-migrate current --db jemalong.db` | Print `cw_revision` |
| `cw-migrate generate --from-program warehouse.lpc` | Diff loaded RECORD classes vs DB (or last revision); write a new revision file |
| `cw-migrate upgrade [--db] [--rev head]` | Apply SQL |
| `cw-migrate downgrade --rev <id>` | Run downgrade SQL |

Revision files (SQL, not Python), e.g. `db/versions/0001_weightnote.sql`:

```
-- revision: 0001
-- down_revision: none
-- upgrade
CREATE TABLE weightnote (
  wn TEXT PRIMARY KEY,
  bales INTEGER NOT NULL DEFAULT 0,
  cores INTEGER NOT NULL DEFAULT 0,
  grabs INTEGER NOT NULL DEFAULT 0
);
-- downgrade
DROP TABLE weightnote;
```

Catalog table `cw_revision(version_num TEXT PRIMARY KEY)` (Alembic’s table name on purpose, but **do not** share a file with WoolSamplingLineAPI Alembic).

Rules:

- Adding an OPTION → `ALTER TABLE … ADD COLUMN` with default.
- Removing an OPTION → explicit revision (not dropped by “load program”).
- Rename → explicit revision only.
- Production does **not** auto-upgrade on iod/dbd/dbsvr start. Operator runs `cw-migrate upgrade`. Mismatch = startup error with expected vs found revision.
- Jemalong’s runtime `"action": "create", "schema": {…}` becomes `cw-migrate` revisions, including `CREATE VIEW` for joined API shapes.
- Raw `action: "sql"` is not the migration tool; it may remain a debug hatch.

**Two-migration-system hazard:** WoolSamplingLineAPI already Alembic-manages `bales.db`. Clockwork must **not** run `cw-migrate` on that file. Coexistence options:

1. **Separate files** (recommended v1): Clockwork `jemalong.db` / `clockwork.db`; Python keeps `bales.db`; later a sync adapter.
2. **Clockwork stays an HTTP client of Python** (fine for Warehouse today; not the native RECORD path, because OPTIONS still need a copy step).
3. **Python becomes a client of datastore’s JSON API** (later cutover PR, optional).

SamplingLine Alembic remains the reference for *operational* warehouse data until an explicit cutover PR.

---

## Language / interface (before → after)

### After (target)

```
WEIGHTNOTE RECORD {
    OPTION wn "" KEY;
    OPTION bales 0;
    OPTION cores 0;
    OPTION grabs 0;
}

BALEDETAILS RECORD {
    OPTION baleId "" KEY;
    OPTION eBaleId "";
    OPTION baleNo "";
    OPTION packCode "N";
    OPTION growerCode "";
    OPTION brand "";
    OPTION appraiser 0;
    OPTION weight 0;
    OPTION cacheWeight NULL;
    OPTION cacheCores NULL;
}

note WEIGHTNOTE;
details BALEDETAILS;
all_notes LIST;
all_bales LIST;

NoteEditor MACHINE note {
    COMMAND list_all {
        CLEAR all_notes;
        COPY ALL FROM WEIGHTNOTE TO all_notes;
    }
    COMMAND queue {
        QUERY JSON_VALUE {
            "action": "select",
            "from": "bale_instance_with_links",
            "where": { "station": "GrabChamber" },
            "order": ["bale_no"]
        } INTO all_bales;
    }
}

# LIST features unchanged once all_notes holds RECORD instances:
#   TAKE FIRST FROM all_notes
#   SORT all_notes BY PROPERTY bales
#   COPY ALL FROM all_notes TO subset WHERE all_notes.ITEM.bales > 0
```

Parser additions: `RECORD` (reject logic in the body), optional `VIEW "name"` / `TABLE "name"`, `KEY`/`UNIQUE`/`NOT NULL` on OPTION, `COPY ALL FROM` RECORD-class source. `QUERY` and persist commands live on MACHINEs (or later FLAG-style builtins). `FIND` stays the **iosh** command (`IODCommandFind`). Joins are JSON/`CREATE VIEW`, not new WHEN syntax.

### INTERFACE layer (kept)

`WEIGHTNOTEINTERFACE` / `BALEDETAILSINTERFACE` / `RecordManager` remain valid against the JSON/channel path. They are how persist works today. RECORD removes the need for a hand-maintained field mapper once replies land on OPTIONS.

### IOD / dbd / datastore protocol

Clockwork surface is still JSON on DATABASE_CHANNEL (Jemalong shape). dbd does not invent a second SQL protocol.

Example find (already valid):

```
{"action":"find","auth":"xxx","type":"weightnote","keys":{"wn":"C0000"},
 "fields":["wn","bales","cores","grabs"]}
```

dbd applies `response` rows onto OPTIONS of the RECORD instance(s), then dependents re-check.

---

## Data model

v1 tables = one per RECORD class (snake or exact class name; pick **lowercase class name**, document it).

Plus system tables:

- `cw_revision`
- `cw_change_log` (if external writers exist)

Do not persist a MACHINE state name on a RECORD: RECORD has no user states.

Warehouse `BaleInstance` mapping is **out of v1 schema**; a later PR may declare a RECORD that matches `app/models.py` if we attach read-only without `cw-migrate`.

---

## Alternatives considered

| Alternative | Why not (as the primary design) |
| --- | --- |
| Stay on JSON+INTERFACE+datastore only | Proved the channel; results are still one JSON property. RECORD is the next step |
| Fold sqlite into `dbd` | Works for one backend; becomes monolithic; datastore already exists to avoid that |
| A new RECORD `MachineInstance` type | Auto-update already lives on MACHINE; extra type is the wrong complexity |
| Keep Warehouse HTTP and add FastAPI push webhooks | Reasonable; still a copy step. Can coexist |
| SQL strings in WHEN / a Clockwork SQL parser | Scan-cycle joins would be slow and hard to sandbox |
| sqlite in each iod | Two plant machines would split writes; EtherCAT must not block |
| SQLAlchemy in Python as the only engine | Already works today; does not give RECORD OPTIONS or inter-CW push |
| Implicit write-through on OPTION assign | Mid-edit writes; dirty loops with inbound PROPERTY; harder HMI |
| RECORD as JSON_VALUE only (no MachineInstance) | LIST features do not apply; WHEN cannot see columns |
| Share `bales.db` and run both Alembic and cw-migrate | Dual heads, guaranteed drift |

---

## Security and privacy

- Auth: CHANNEL `KEY` already exists (`database_channel.lpc`). Datastore’s JSON `auth` placeholder can stay on the shim until token restrictions land.
- JSON → SQL uses an identifier allow-list (tables, views, columns from the catalog) and bound parameters. No concatenated SQL from LPC.
- Raw `action: "sql"` if kept is operator-only and still parameterized; not used for migrations.
- Database file permissions = plant user that runs **datastore**, not iod.
- Do not log full row payloads at default debug (RFID tags are identifiers).

---

## Observability

- datastore: log action, type, duration, errors; not full rows at info.
- dbd: log forward latency and PROPERTY apply; `last_error` on the instance if useful.
- Metrics: commit latency, clients connected, PROPERTY apply count (station queues are tens of rows; target well under HTTP RTT).
- `iosh` / `FIND` can list RECORD instances.

---

## Risks

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Dual writers on one sqlite file | High | One `dbsvr`; both iods (via dbd) are clients |
| datastore or LAN down | High | Plant keeps last OPTIONS; HTTP path remains fallback until cutover |
| Echo persist between two iods | High | Inbound PROPERTY does not persist again |
| Schema drift vs WoolSamplingLineAPI | High | Separate DB files until an explicit cutover PR |
| Instance explosion (`find_all` 100k rows) | Medium | COPY is a COMMAND; cap / paging later; warehouse scale is small |
| PROPERTY storm on wide rows | Medium | Only send changed columns; coalesce per cycle |
| Parser `RECORD` vs user class named RECORD | Low | Keyword; same as MACHINE |
| Dynamic instance lifetime / leaks | Medium | Registry + optional LRU for unbound query hits; named instances never evicted |
| Migration applied on wrong file | High | Refuse mismatch; never auto-upgrade prod |
| Second iod misses a write | High | Open: notify-after-commit vs refresh. Do not “fix” by merging sqlite into dbd |

---

## Rollout

1. Language: RECORD grammar (no logic in the body). **No Warehouse LPC changes in early PRs.**
2. dbd applies datastore JSON rows onto OPTIONS. Existing `dbsvr` is enough for round-trip tests.
3. datastore: parameterized SQL, joins/views JSON.
4. Two iods / two dbds / one `dbsvr` (sim of Grab/Core): persist on A updates OPTIONS on B.
5. `cw-migrate` including `CREATE VIEW`.
6. Docs + Jemalong RECORD example. Warehouse HTTP stays until a later PR.

Rollback: leave RECORD unused; old HTTP path unchanged; datastore/dbd as today.

---

## Testing

New tests beside `tests/datastore.cw`, `tests/db-channel.cw`, `tests/lists.cw`, `tests/copy.cw`:

- RECORD with OPTIONS parses; WHEN/COMMAND in the RECORD body is a parse error.
- Persist round-trip → OPTIONS filled; a MACHINE that depends on them re-checks; **no** FIND required for that.
- Two simulated iods: persist on A updates OPTIONS on B; B does not persist back.
- JSON `join` / named VIEW query into a LIST (flatten like `bale_with_links_dict`).
- `COPY ALL FROM WEIGHTNOTE TO list` then `TAKE FIRST`, `SORT BY PROPERTY`, `SIZE OF`.
- Local assign does not hit datastore until explicit persist; inbound PROPERTY does not persist again.
- Migration generate/upgrade add column; start without upgrade refuses.
- Negative: COPY FROM RECORD with non-column predicate errors.

C++: datastore `SQLInterface` / Store tests in the datastore repo; dbd apply-OPTIONS tests in `iod/tests/`.

---

## Open questions

These do not block the recommended design; they affect later cutover and remaining author review.

1. **Cutover target for Warehouse:** keep WoolSamplingLineAPI as operational SoT and add a RECORD *projection*, or move operational `BaleInstance` into Clockwork’s DB and make FastAPI a client?
2. **Where `dbsvr` runs on the plant:** one of the two warehouse PCs, or a small third box both iods already reach for the API?
3. **Table naming:** exact class name `WEIGHTNOTE` vs `weightnote` vs explicit `TABLE "weight_notes"`?
4. **Composite keys** in v1, or single KEY plus views for `(wn, bale_no)` lookups?
5. **QUERY JSON richness in v1:** named views only, or ad-hoc `join` arrays from LPC as well?
6. **Two-Clockwork notify:** datastore PUB after commit, dbd refresh, or another mechanism?
7. **Builtin persist messages** (FLAG-style `save`/`load` on RECORD) vs INTERFACE-only?

---

## Key decisions

1. **`RECORD` is MACHINE with a lex/bison limit (no user handlers or states).** OPTIONS are columns of a table or view. No new instance type.
2. **Keep datastore as the database server; keep `dbd` as the channel adapter.** Do not fold sqlite into `dbd`.
3. **JSON in Clockwork; Store ops in datastore** (sqlite today, other backends later). LPC does not parse SQL. Named views cover today’s API flatten.
4. **Explicit persist, not write-through** — mid-edit writes and echo loops stay out. INTERFACE + DATABASE_CHANNEL remains the persist path until builtins are specified.
5. **Per-column PROPERTY after a successful reply** — dependents already run. No extra GET on that iod.
6. **`COPY ALL FROM` / `QUERY json INTO list` reuse LIST** — FIND stays iosh; WHEN stays off SQL.
7. **Alembic-like revisions including `CREATE VIEW`; no auto-upgrade on start.**
8. **Do not share `bales.db` with Python Alembic in v1.**
9. **JemalongDB first in examples/tests; Warehouse HTTP until a later PR.**

---

## References

- `../datastore` — JSON/ZMQ database server (`dbsvr`, `SQLInterface`, `Store`, README backends)
- `iod` `dbd` — current blob-response DATABASE_CHANNEL adapter
- `tests/datastore.cw`, `tests/db-channel.cw` — first language sketch for DATABASE_CHANNEL
- `/Users/mike/src/latproc/WarehouseSIM/JemalongDB/*` — real attempt under current features
- `/Users/mike/src/latproc/Warehouse/lib/api/samplingline_api.lpc`, `lib/BaleObject.lpc`, `machine/Panel.lpc` — HTTP + ChangeCounter path
- `SamplingLineProjects/WoolSamplingLineAPI/app/models.py`, `app/utils.py` `bale_with_links_dict`, `specs/02-api-contract.md`, `specs/03-data-model-and-migrations.md` — joins/views-as-API and Alembic
- Warehouse `config/Grab` and `config/Core` — two iods sharing the HTTP API today
- `iod/src/clockwork.cpp` FLAG/LIST `MachineClass`, `iod/src/cwlang.ypp` COPY ALL FROM, `MachineInstance::notifyDependents`

---

## PR Plan

Implementation on a new branch; each PR independently reviewable. Clockwork PRs and datastore PRs stay in their own repos.

### PR 1 — RECORD grammar (Clockwork)

- **Title:** Add RECORD class syntax; reject logic in the body
- **Files:** `iod/src/cwlang.lpp`, `cwlang.ypp`, `MachineClass.h/.cpp`, tests that parse OPTIONS-only RECORD and reject WHEN/COMMAND/states
- **Depends:** none
- **Description:** `Name RECORD { OPTION … }` parses like MACHINE. `is_record` flag. Instances are `MachineInstance`. No dbd/datastore change.

### PR 2 — Apply datastore rows onto OPTIONS (Clockwork)

- **Title:** dbd maps JSON response rows onto RECORD OPTIONS
- **Files:** `dbd`, PROPERTY apply, skip echo persist, tests with existing `dbsvr` or a fixture
- **Depends:** PR 1
- **Description:** Per-column PROPERTY instead of one JSON blob. Dependents re-check. INTERFACE path still works.

### PR 3 — Parameterized JSON→SQL and joins (datastore)

- **Title:** Bound parameters, identifier allow-list, select/join/view JSON
- **Files:** `../datastore` `SQLInterface`, `Store`, tests
- **Depends:** none (can parallel PR 1)
- **Description:** Finish the JSON compiler in datastore. sqlite stays a Store. No sqlite in `dbd`.

### PR 4 — Two Clockworks, one datastore

- **Title:** Grab persist updates Core OPTIONS
- **Files:** dbd and/or datastore notify (mechanism from open question 6), two-process test
- **Depends:** PR 2, PR 3
- **Description:** Shared `dbsvr`. Echo-loop tests. This is the plant-sharing requirement.

### PR 5 — COPY ALL FROM RecordClass INTO LIST (Clockwork)

- **Title:** Materialize RECORD queries into LIST
- **Files:** COPY parser, dbd/datastore find, `tests/record_list.cw`
- **Depends:** PR 2, PR 3
- **Description:** PK registry reuse. TAKE/SORT/SIZE/WHERE-on-LIST work. Simple `where` push-down.

### PR 6 — VIEW-backed RECORD and QUERY INTO

- **Title:** VIEW-backed RECORD and QUERY JSON INTO list
- **Files:** Clockwork VIEW on class, QUERY on MACHINE; datastore view/`select`
- **Depends:** PR 5
- **Description:** Named `CREATE VIEW` plus ad-hoc JSON joins. LIST of joined RECORDs.

### PR 7 — cw-migrate (tables and views)

- **Title:** Alembic-style cw-migrate including CREATE VIEW
- **Files:** datastore-adjacent `cw-migrate`, `db/versions/`, start-time revision check
- **Depends:** PR 1, PR 3
- **Description:** generate/upgrade/downgrade/current. Views for joined API shapes. No auto-apply in production.

### PR 8 — JemalongDB example rewrite (docs + sim only)

- **Title:** Rewrite WarehouseSIM/JemalongDB to RECORD
- **Files:** JemalongDB or `latproc/examples/`, language_ref
- **Depends:** PR 5, PR 7
- **Description:** Same weight-note and bale-details as RECORD. INTERFACE remains the persist bridge. No plant Warehouse change.

### PR 9 (optional, later) — Warehouse projection

- **Title:** Optional BALEDETAILAPI backed by RECORD + shared datastore
- **Files:** Warehouse LPC (Grab and Core), optional Python JSON client of `dbsvr`
- **Depends:** PR 4, PR 6, PR 8, Open Question 1
- **Description:** Both iods use datastore; station panels can skip refresh GET for CW-visible rows.
