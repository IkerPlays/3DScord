#!/usr/bin/env python3
"""Compares the language files with en_US.json: invalid JSON or keys that do not exist in English fail the check; missing keys are only reported
(a missing key falls back to English). Usage: python3 tools/check_lang.py"""
import glob, json, os, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "romfs", "lang")
with open(os.path.join(root, "en_US.json"), encoding="utf-8") as f:
    reference = json.load(f)
failed = False
for path in sorted(glob.glob(os.path.join(root, "*.json"))):
    name = os.path.basename(path)
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print("%s: invalid JSON: %s" % (name, e))
        failed = True
        continue
    missing = [k for k in reference if k not in data]
    extra = [k for k in data if k not in reference]
    placeholders = [k for k in data if k in reference and sorted(x for x in ("{0}", "{1}", "{2}") if x in data[k]) != sorted(x for x in ("{0}", "{1}", "{2}") if x in reference[k])]
    print("%s: %d keys, %d missing (English is used), %d unknown, %d with other placeholders" % (name, len(data), len(missing), len(extra), len(placeholders)))
    if extra and name != "en_US.json":
        print("  unknown keys:", ", ".join(extra[:10]), "..." if len(extra) > 10 else "")
        failed = True
    if placeholders:
        print("  placeholders differ in (check them):", ", ".join(placeholders[:10]))
sys.exit(1 if failed else 0)
