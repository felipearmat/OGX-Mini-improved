#!/usr/bin/env python3
"""Check every relative link in the repository's Markdown files.

Fails when a link points to a file that does not exist (compared case-sensitively, as on
GitHub and Linux), to a file that is not tracked by git (a local-only document), or to a
#heading anchor that the target file does not have. External links (http, mailto) are not
fetched.

Usage: check_doc_links.py [repo root]   (default: the current directory)
"""
import os
import pathlib
import re
import subprocess
import sys

SKIP_PREFIXES = ("Firmware/external/", "Firmware/ESP32_Blueretro/", "Tools/dump-dvd-kit/")
LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)|!\[[^\]]*\]\(([^)\s]+)\)|<img[^>]+src=\"([^\"]+)\"")
FENCE = re.compile(r"^\s*(```|~~~)")


def tracked_files(repo: pathlib.Path):
    out = subprocess.run(["git", "-C", str(repo), "ls-files"], check=True, capture_output=True, text=True)
    return set(out.stdout.splitlines())


def slug(heading: str) -> str:
    """GitHub's heading anchor: rendered text, lower case, punctuation dropped, spaces to '-'."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # links / images -> their text
    text = re.sub(r"<[^>]+>", "", text)                          # inline HTML
    text = text.replace("`", "").replace("*", "").replace("~", "")
    text = re.sub(r"(?<!\w)_|_(?!\w)", "", text)                 # emphasis underscores
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def anchors(path: pathlib.Path):
    found, counts, in_fence = set(), {}, False
    for line in path.read_text(errors="replace").splitlines():
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = re.match(r"^\s{0,3}#{1,6}\s+(.*?)\s*#*\s*$", line)
        if match:
            base = slug(match.group(1))
            n = counts.get(base, 0)
            counts[base] = n + 1
            found.add(base if n == 0 else f"{base}-{n}")
        for explicit in re.findall(r"<a\s+(?:name|id)=\"([^\"]+)\"", line):
            found.add(explicit)
    return found


def exists_exact_case(repo: pathlib.Path, rel: pathlib.PurePosixPath) -> bool:
    path = repo
    for part in rel.parts:
        if not path.is_dir() or part not in os.listdir(path):
            return False
        path = path / part
    return True


def main() -> int:
    repo = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    tracked = tracked_files(repo)
    tracked_dirs = {str(pathlib.PurePosixPath(f).parent) for f in tracked}
    for f in list(tracked_dirs):
        p = pathlib.PurePosixPath(f)
        while str(p) not in (".", ""):
            tracked_dirs.add(str(p))
            p = p.parent
    docs = sorted(f for f in tracked if f.endswith(".md") and not f.startswith(SKIP_PREFIXES))
    anchor_cache, errors = {}, []

    for doc in docs:
        in_fence = False
        for number, line in enumerate((repo / doc).read_text(errors="replace").splitlines(), 1):
            if FENCE.match(line):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            line = re.sub(r"`[^`]*`", "", line)  # links inside inline code are not links
            for groups in LINK.findall(line):
                target = next(g for g in groups if g)
                if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I):
                    continue  # http:, https:, mailto: ...
                path_part, _, anchor = target.partition("#")
                where = f"{doc}:{number}: {target}"
                if path_part:
                    rel = os.path.normpath(os.path.join(os.path.dirname(doc), path_part)).replace(os.sep, "/")
                    if rel.startswith("../"):
                        errors.append(f"{where} -> points outside the repository")
                        continue
                    rel_path = pathlib.PurePosixPath(rel.rstrip("/"))
                    if not exists_exact_case(repo, rel_path):
                        errors.append(f"{where} -> no such file (case-sensitive)")
                        continue
                    if str(rel_path) not in tracked and str(rel_path) not in tracked_dirs:
                        errors.append(f"{where} -> file is not tracked by git (local only)")
                        continue
                    target_file = rel_path
                else:
                    target_file = pathlib.PurePosixPath(doc)
                if anchor and str(target_file).endswith(".md"):
                    if target_file not in anchor_cache:
                        anchor_cache[target_file] = anchors(repo / target_file)
                    if anchor not in anchor_cache[target_file]:
                        errors.append(f"{where} -> no heading #{anchor} in {target_file}")

    if errors:
        print("Documentation link check failed:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"Documentation links OK: {len(docs)} Markdown files checked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
