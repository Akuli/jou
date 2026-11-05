# This file was used to auto-generate most of stdlib/windows_api/errors.jou.

import json
import sys
import os
import requests
import re
import time

urls = [
    "https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-",
    "https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--500-999-",
    "https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1000-1299-",
    "https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1300-1699-",
    "https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1700-3999-",
    "https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--4000-5999-",
]

# Go to a consistent working directory regardless of where script is ran from
if not os.path.isdir("stdlib"):
    os.chdir("..")
assert os.path.isdir("stdlib")

try:
    os.mkdir("tmp")
except FileExistsError:
    pass

try:
    with open("tmp/winerror.json", "r") as file:
        error_codes = json.load(file)
except FileNotFoundError:
    print("Getting error codes from documentation (may take a while...)")

    error_codes = []
    for url in urls:
        if url != urls[0]:
            print("polite sleep...")
            time.sleep(1)
        print("GET", url)
        r = requests.get(url)
        r.raise_for_status()
        matches = re.findall(r"([^>]*)</strong></p>\s*<p>(\d+)[^<]*</p>\s*<p>([^<]*)", r.text)
        print(f"  {len(matches)} matches")
        assert matches
        for name, number, desc in matches:
            error_codes.append({"name": name, "number": int(number), "description": desc})

    with open("tmp/winerror.json", "w") as file:
        json.dump(error_codes, file, indent=2)
        file.write("\n")

    print()

print("# Sources:")
for url in urls:
    print(f"#   {url}")
print()

for ec in error_codes:
    # The description is not used here, because I don't know what copyright
    # applies to them.
    name = ec['name']
    number = ec['number']

    # This skips a few that I don't think we need yet
    if name.startswith("ERROR_"):
        print("@public")
        print(f"const {name}: uint32 = {number}")
