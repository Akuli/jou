"""Check that links in markdown files point to reasonable places."""
# There is a similar script in https://github.com/Akuli/porcupine/

import argparse
import json
import os
import re
import subprocess
import sys
from http.client import responses as status_code_names
from pathlib import Path
from datetime import datetime, timedelta

import requests

PROJECT_ROOT = Path(__file__).absolute().parent
assert (PROJECT_ROOT / "README.md").is_file()
os.chdir(PROJECT_ROOT)


def find_markdown_files():
    output = subprocess.check_output(["git", "ls-files", "*.md"], text=True)
    return [Path(line) for line in output.splitlines()]


def find_links_in_file(markdown_file_path):
    content = markdown_file_path.read_text(encoding="utf-8")

    for lineno, line in enumerate(content.splitlines(), start=1):
        link_regexes = [
            # [text](target)
            # The text can contain spaces, and it can even be split across many lines.
            # This also detects images ![text](target) and that's a good thing.
            r"\]\(([^\s()]+(?:\([^\s()]+\))?)\)",
            # [blah blah]: target
            r"^\[[^\[\]]+\]: (.+)$",
        ]
        for regex in link_regexes:
            for target in re.findall(regex, line):
                yield (lineno, target)


def check_https_link(url):
    # Give website owners some sort of idea why we are doing these requests.
    headers = {
        "User-Agent": "check-markdown-links.py/1.0 (+https://github.com/Akuli/jou)"
    }

    try:
        # Many sites redirect to front page for bad URLs. Let's not treat that as ok.
        response = requests.head(url, timeout=10, allow_redirects=False, headers=headers)
    except requests.exceptions.RequestException as e:
        return f"HTTP HEAD request failed: {e}"

    if url == "https://github.com/Akuli/jou/issues/new":
        # It returns 302 Found, because it redirects to login page
        expected_statuses = [302]
    elif url.startswith("https://stackoverflow.com/questions/"):
        # Starting in June 2025, stackoverflow returns 403 Forbidden for some reason
        #expected_status = 403
        # In Late July 2025, stackoverflow returns 200 again. Let's accept both.
        expected_statuses = [200, 403]
    else:
        expected_statuses = [200]

    if response.status_code not in expected_statuses:
        return f"site returns {response.status_code} {status_code_names[response.status_code]}"

    return "ok"


def get_all_refs(path):
    result = []
    for title in re.findall(r"\n#+ (.*)", path.read_text()):
        # Try to match whatever GitHub does.
        #
        # Examples:
        #   "## Combining multiple cases with `|`" --> #combining-multiple-cases-with-
        #   "## `byte`, `int`, `int64`" --> #byte-int-int64
        #   "## Undefined Behavior (UB)" --> #undefined-behavior-ub
        #   "## Rust's approach to UB" --> #rusts-approach-to-ub
        #
        # This is not documented anywhere, see e.g. https://stackoverflow.com/a/73742961
        title = title.lower().strip().replace(" ", "-")
        title = re.sub(r"[^\w-]", "", title)
        result.append("#" + title)
    return result


def link_ok_recently(target, threshold):
    """Check if the link was recently ok (within the threshold time)."""
    try:
        with open("links.json", "r") as file:
            links = json.load(file)
    except FileNotFoundError:
        return False

    if target not in links:
        return False

    time_since_check = datetime.now() - datetime.fromisoformat(links[target])
    return time_since_check < threshold


def link_ok_now(target):
    """Mark the link as 'ok' by saving the current timestamp."""
    try:
        with open("links.json", "r") as file:
            links = json.load(file)
    except FileNotFoundError:
        links = {}

    links[target] = datetime.now().isoformat()
    with open("links.json", "w") as file:
        json.dump(links, file, indent=4)


def check_link(markdown_file_path, target, offline_mode=False):
    if target.startswith("http://"):
        return "this link should probably use https instead of http"

    if target.startswith("https://"):
        if offline_mode:
            return "assume ok (offline mode)"
        if link_ok_recently(target, timedelta(minutes=1)):
            return "assume ok (was ok less than 1 minute ago)"

        result = check_https_link(target)

        if result == "ok":
            link_ok_now(target)
        if result != "ok" and link_ok_recently(target, timedelta(days=3)):
            return f"assume ok because it was ok less than 3 days ago, fails now: {result}"
        return result

    if "//" in target:
        return "double slashes are allowed only in http:// and https:// links"

    if "\\" in target:
        return "use forward slashes instead of backslashes"

    if target.startswith('#'):
        # Link to within same file
        path = markdown_file_path
    else:
        path = markdown_file_path.parent / target.split("#")[0]

    if PROJECT_ROOT not in path.resolve().parents:
        return "link points outside of the Jou project folder"

    if not path.exists():
        return "link points to a file or folder that doesn't exist"

    if "#" in target:
        # Reference to title within markdown file.
        # For example: architecture-and-design.md#loading-order
        if (not path.is_file()) or path.suffix != ".md":
            return "hashtag '#' can only be used with markdown files"

        refs = get_all_refs(path)
        ref = "#" + target.split("#", 1)[1]
        if ref not in refs:
            return f"no heading in {path} matches {ref} (should be one of: {' '.join(refs)})"

    return "ok"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--offline",
        action="store_true",
        help="don't do HTTP requests, just assume that https:// links are fine",
    )
    args = parser.parse_args()

    good_links = 0
    bad_links = 0

    for path in find_markdown_files():
        for lineno, target in find_links_in_file(path):
            result = check_link(path, target, offline_mode=args.offline)
            print(f"{path}:{lineno}: {result}")
            if result == "ok" or result.startswith("assume ok"):
                good_links += 1
            else:
                bad_links += 1

    if good_links + bad_links == 0:
        print("Error: no links found")
        sys.exit(1)

    print()
    print(f"checked {good_links + bad_links} links: {good_links} good, {bad_links} bad")
    if bad_links > 0:
        sys.exit(1)


main()
