#!/usr/bin/env python3
"""Check that each patched dependency tree (Bluepad32, its BTstack, the Pico SDK) holds exactly what
its patches give on a clean checkout of its commit (OGX-Mini-improved).

Firmware/cmake/patch_libs.cmake applies the patches and keeps a stamp of them; it leaves a tree
alone while the stamp matches, so an edit made in the tree by hand (or a tree patched another way)
would go on being built locally while CI builds the patches. This script lists every file the
patches touch or add, applies them to a throwaway git worktree of the same commit and compares,
and reports files changed in the tree that no patch accounts for. The patch lists are read from
patch_libs.cmake (its ogxm_patch_tree calls), so they live in one place.

Exit status 0 when every tree matches; 1 with what differs. A tree that is not checked out (the
Pico SDK outside the repository) is skipped.

    python3 Tools/check_patched_trees.py
"""
import pathlib
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
EXTERNAL = REPO / "Firmware" / "external"
PATCH_LIBS = REPO / "Firmware" / "cmake" / "patch_libs.cmake"


def git(cwd, *args, check=True):
    return subprocess.run(["git", *args], cwd=cwd, check=check, capture_output=True, text=True).stdout


def patch_lists():
    """{tree name: (tree dir, [patch paths])} from the ogxm_patch_tree calls."""
    text = PATCH_LIBS.read_text()
    trees = {}
    for match in re.finditer(r'ogxm_patch_tree\(\s*"([^"]+)"\s+"([^"]+)"(.*?)\)', text, re.S):
        name, directory, rest = match.groups()
        if "${NAME}" in name or name == "NAME":
            continue  # the function's own definition
        directory = directory.replace("${EXTERNAL_DIR}", str(EXTERNAL)).replace("${_sdk}", str(EXTERNAL / "pico-sdk"))
        patches = [p.replace("${_p}", str(EXTERNAL / "patches")).replace("${EXTERNAL_DIR}", str(EXTERNAL))
                   for p in re.findall(r'"([^"]+\.diff)"', rest)]
        trees[name] = (pathlib.Path(directory), [pathlib.Path(p) for p in patches])
    if not trees:
        sys.exit(f"no ogxm_patch_tree calls found in {PATCH_LIBS}")
    return trees


def changed_files(tree):
    """Files that differ from the commit, and files added (not ignored); nested submodules are
    trees of their own."""
    modified = set(filter(None, git(tree, "diff", "--name-only", "--ignore-submodules=all", "HEAD").splitlines()))
    added = set(filter(None, git(tree, "ls-files", "--others", "--exclude-standard").splitlines()))
    return modified | added


def check(name, tree, patches):
    if not (tree / ".git").exists():
        print(f"{name}: {tree} not checked out, skipped")
        return []
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        clean = pathlib.Path(tmp) / "tree"
        git(tree, "worktree", "add", "--detach", "--quiet", str(clean), "HEAD")
        try:
            for patch in patches:
                result = subprocess.run(["git", "apply", "--ignore-whitespace", str(patch)], cwd=clean,
                                        capture_output=True, text=True)
                if result.returncode != 0:
                    problems.append(f"{name}: {patch.name} does not apply to a clean checkout: {result.stderr.strip()}")
                    return problems
            expected = changed_files(clean)
            actual = changed_files(tree)
            for path in sorted(expected | actual):
                a, b = tree / path, clean / path
                if path not in expected:
                    problems.append(f"{name}: {path} differs from the commit but no patch changes it")
                elif path not in actual:
                    problems.append(f"{name}: {path} is not patched in the tree")
                elif a.read_bytes() != b.read_bytes():
                    problems.append(f"{name}: {path} differs from what the patches give")
        finally:
            git(tree, "worktree", "remove", "--force", str(clean), check=False)
    if not problems:
        print(f"{name}: {len(patches)} patches, tree matches")
    return problems


def main():
    problems = []
    for name, (tree, patches) in patch_lists().items():
        problems += check(name, tree, patches)
    for problem in problems:
        print(problem)
    if problems:
        print("Rebuild to reapply the patches (or remove the tree's ogxm-patches.sha256 stamp); "
              "change dependency code through Firmware/external/patches only.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
