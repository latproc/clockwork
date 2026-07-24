#!/usr/bin/env python3
"""Audit or convert Clockwork EtherCAT flattened entry positions.

Conversion replaces the legacy ordinal with positional object identity:
module,index,subindex and, only when required, a PDO index discriminator.
No file is changed unless every candidate resolves and --write is supplied.
"""

import argparse
import datetime
import difflib
import json
import pathlib
import re
import shutil
import sys

IO_TYPES = (
    "POINT", "ANALOGINPUT", "COUNTERRATE", "COUNTER", "STATUS_FLAG",
    "DIGITALVALUE", "ANALOGOUTPUT",
)
MODULE_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\bposition\s*:\s*(\d+)[^;]*\)\s*;"
)
DECL_RE = re.compile(
    r"^(?P<head>\s*[A-Za-z_][A-Za-z0-9_]*\s+(?:"
    + "|".join(IO_TYPES)
    + r")\s+)(?P<options>\([^)]*\)\s+)?"
      r"(?P<module>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
      r"(?P<position>0[xX][0-9a-fA-F]+|\d+)(?P<tail>\s*(?:,[^;]*)?;.*)$"
)
INDEX_RE = re.compile(r"0[xX]([0-9a-fA-F]+)")
POSITIONAL_SELECTOR_TAIL_RE = re.compile(
    r"^\s*,\s*(?:0[xX][0-9a-fA-F]+|\d+)(?:\s*,|;)"
)


def parse_number(value):
    if isinstance(value, int):
        return value
    match = INDEX_RE.search(value)
    if match:
        return int(match.group(1), 16)
    return int(value, 0)


def load_modules(path):
    modules = {}
    for line_no, line in enumerate(path.read_text().splitlines(), 1):
        match = MODULE_RE.match(line)
        if not match:
            continue
        name, position = match.group(1), int(match.group(2))
        if name in modules:
            raise ValueError(f"{path}:{line_no}: duplicate module {name}")
        modules[name] = position
    return modules


def load_topology(path):
    devices = json.loads(path.read_text())
    result = {}
    for device in devices:
        position = int(device["position"])
        entries = []
        for sync in device.get("sync_managers", []):
            for pdo in sync.get("pdos", []):
                pdo_index = parse_number(pdo["index"])
                for entry in pdo.get("entries", []):
                    entries.append({
                        "position": int(entry["pos"]),
                        "index": parse_number(entry["index"]),
                        "subindex": int(entry["subindex"]),
                        "pdo_index": pdo_index,
                    })
        result[position] = entries
    return result


def selector_values(entry, entries):
    duplicates = [
        item for item in entries
        if item["index"] == entry["index"]
        and item["subindex"] == entry["subindex"]
    ]
    fields = [f"0x{entry['index']:04X}", str(entry["subindex"])]
    if len(duplicates) > 1:
        fields.append(f"0x{entry['pdo_index']:04X}")
    return ", ".join(fields)


def convert_text(text, source, modules, topology):
    errors = []
    converted = 0
    output = []
    for line_no, line in enumerate(text.splitlines(keepends=True), 1):
        match = DECL_RE.match(line.rstrip("\n"))
        if not match:
            output.append(line)
            continue
        if POSITIONAL_SELECTOR_TAIL_RE.match(match.group("tail")):
            output.append(line)
            continue
        options = match.group("options") or ""
        module_name = match.group("module")
        if module_name not in modules:
            errors.append(f"{source}:{line_no}: module {module_name} has no position mapping")
            output.append(line)
            continue
        module_position = modules[module_name]
        entries = topology.get(module_position)
        if entries is None:
            errors.append(
                f"{source}:{line_no}: module {module_name} position {module_position} "
                "is absent from topology"
            )
            output.append(line)
            continue
        legacy_position = int(match.group("position"), 0)
        matches = [item for item in entries if item["position"] == legacy_position]
        if len(matches) != 1:
            errors.append(
                f"{source}:{line_no}: {module_name} legacy position {legacy_position} "
                f"resolved {len(matches)} entries"
            )
            output.append(line)
            continue
        selector = selector_values(matches[0], entries)
        newline = (
            match.group("head") + options + module_name + ", "
            + selector + match.group("tail")
        )
        if line.endswith("\n"):
            newline += "\n"
        output.append(newline)
        converted += 1
    return "".join(output), converted, errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modules", type=pathlib.Path, required=True)
    parser.add_argument("--topology", type=pathlib.Path, required=True)
    parser.add_argument("--write", action="store_true",
                        help="write converted files after creating timestamped backups")
    parser.add_argument("files", nargs="+", type=pathlib.Path)
    args = parser.parse_args(argv)

    try:
        modules = load_modules(args.modules)
        topology = load_topology(args.topology)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    changes = []
    errors = []
    for path in args.files:
        try:
            original = path.read_text()
        except OSError as exc:
            errors.append(f"{path}: {exc}")
            continue
        converted, count, file_errors = convert_text(original, path, modules, topology)
        errors.extend(file_errors)
        if converted != original:
            changes.append((path, original, converted, count))

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print("No files changed.", file=sys.stderr)
        return 1

    total = sum(item[3] for item in changes)
    if not args.write:
        for path, original, converted, _ in changes:
            sys.stdout.writelines(difflib.unified_diff(
                original.splitlines(keepends=True),
                converted.splitlines(keepends=True),
                fromfile=str(path), tofile=str(path) + " (converted)",
            ))
        print(f"Dry run: {total} declarations in {len(changes)} files would change.",
              file=sys.stderr)
        return 0

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for path, original, converted, _ in changes:
        backup = path.with_name(path.name + f".bak.{stamp}")
        if backup.exists():
            print(f"error: backup already exists: {backup}", file=sys.stderr)
            print("No files changed.", file=sys.stderr)
            return 1
    for path, original, converted, _ in changes:
        backup = path.with_name(path.name + f".bak.{stamp}")
        shutil.copy2(path, backup)
        path.write_text(converted)
        print(f"updated {path}; backup {backup}")
    print(f"Converted {total} declarations in {len(changes)} files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
