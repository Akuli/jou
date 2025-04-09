"""Check that links in markdown files point to reasonable places."""
# Get from https://github.com/Akuli/porcupine/

import argparse
import os
import re
import subprocess
import sys
import time
from functools import cache
from http.client import responses as status_code_names
from pathlib import Path

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
                yield (markdown_file_path, lineno, target)


@cache
def check_https_link(url):
    # TODO: delete when Advent of Code stops returning 403 for our requests
    if "adventofcode.com" in url:
        return "ok"

    try:
        # Many sites redirect to front page for bad URLs. Let's not treat that as ok.
        response = requests.head(url, timeout=10, allow_redirects=False)
    except requests.exceptions.RequestException as e:
        return f"HTTP HEAD request failed: {e}"

    if url == "https://github.com/Akuli/jou/issues/new":
        # It returns 302 Found, because it redirects to login page
        expected_status = 302
    else:
        expected_status = 200

    if response.status_code != expected_status:
        return f"site returns {response.status_code} {status_code_names[response.status_code]}"

    return "ok"


def get_all_refs(path):
    result = []
    for title in re.findall(r"\n#+ (.*)", path.read_text()):
        words = re.findall(r"[a-z0-9]+", title.lower().replace("'", ""))
        result.append("#" + "-".join(words))
    return result


def check_link(markdown_file_path, target, offline_mode=False):
    if target.startswith("http://"):
        return "this link should probably use https instead of http"

    if target.startswith("https://"):
        if offline_mode:
            return "assume ok (offline mode)"
        return check_https_link(target)

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
    parser.add_argument(
        "--retry",
        type=int,
        default=1,
        metavar="n",
        help="retry n times when https links don't work",
    )
    args = parser.parse_args()

    remaining_links = []
    for path in find_markdown_files():
        for link in find_links_in_file(path):
            remaining_links.append(link)

    if not remaining_links:
        print("Error: no links found")
        sys.exit(1)

    good_links = 0
    bad_links = 0

    assert args.retry >= 1
    for i in reversed(range(args.retry)):
        if i == 0:
            # Do not retry anymore
            retry = None
        else:
            retry = []

        for link in remaining_links:
            path, lineno, target = link
            result = check_link(path, target, offline_mode=args.offline)
            print(f"{path}:{lineno}: {result}")
            if result == "ok" or result == "assume ok (offline mode)":
                good_links += 1
            elif target.startswith("https://") and retry is not None:
                retry.append(link)
            else:
                bad_links += 1

        remaining_links.clear()
        if retry:
            print(f"Sleeping for 10 seconds before trying {len(retry)} bad links again.")
            print()
            time.sleep(10)
            remaining_links = retry
            check_https_link.cache_clear()

    assert good_links + bad_links > 0
    print()
    print(f"checked {good_links + bad_links} links: {good_links} good, {bad_links} bad")
    if bad_links > 0:
        sys.exit(1)


main()
