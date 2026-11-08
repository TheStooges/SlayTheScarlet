"""Packages the Release build as release/SlayTheScarlet-<version>.zip, for attaching to a GitHub release.

Usage, after tools/build_release.bat:
    python tools/package_release.py 0.2
    python tools/package_release.py 0.2 --dirty     (allow uncommitted changes, for trying it out)

Beside the zip it keeps release/SlayTheScarlet-<version>-debug/, holding the matching PDB and DLL
for reading crash dumps, and a note of the commit it was built from. Keep that folder, and do not
publish it. release/ is gitignored.

The zip is read back afterwards: every file is compared with its source, and every file is searched
for the builder's Windows user name and local paths, which must never be published."""
import argparse, hashlib, io, os, re, shutil, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build-release", "mod")

# Path inside the zip -> source in the repository.
CONTENTS = {
    "SlayTheScarlet.dll": os.path.join(BUILD, "SlayTheScarlet.dll"),
    "cards.csv": os.path.join(ROOT, "data", "cards.csv"),
    "settings.ini": os.path.join(ROOT, "data", "settings.ini"),
    "lang/ja.txt": os.path.join(ROOT, "data", "lang", "ja.txt"),
    "README.md": os.path.join(ROOT, "README.md"),
    "CHANGELOG.md": os.path.join(ROOT, "CHANGELOG.md"),
    "LICENSE.txt": os.path.join(ROOT, "LICENSE"),
    "LICENSE-SokuLib.txt": os.path.join(ROOT, "lib", "SokuLib", "LICENSE"),
}

# The licence names its copyright holder on purpose, which may match the builder's user name. It is
# still checked for local paths.
NAMES_ITS_AUTHOR = {"LICENSE.txt"}

# Translations are UTF-8 by design; everything else must be ASCII.
UTF8_TEXT = {"lang/ja.txt"}

# The README's picture of the Deck 1 symbol is left out of the zip, as plain words.
README_IMAGE = b"(![green Deck 1 symbol](images/deck1.svg))"
README_IMAGE_TEXT = b"(the first deck slot)"


def git(*args):
    return subprocess.check_output(["git", "-C", ROOT] + list(args)).decode("ascii", "replace").strip()


def zip_readme():
    readme = open(CONTENTS["README.md"], "rb").read()
    if readme.count(README_IMAGE) != 1:
        sys.exit("README.md: expected the Deck 1 image once, found it %d times. Update README_IMAGE."
                 % readme.count(README_IMAGE))
    readme = readme.replace(README_IMAGE, README_IMAGE_TEXT)
    if b"images/" in readme:
        sys.exit("README.md refers to another image, which the zip does not include.")
    return readme


def private_strings(inside):
    """What would identify the builder's machine, built from this machine rather than written here."""
    found = [b"Users\\", b"Users/"]
    names = ("USERPROFILE",) if inside in NAMES_ITS_AUTHOR else ("USERNAME", "USERPROFILE")
    for name in names:
        value = os.environ.get(name)
        if value:
            found.append(value.encode("ascii", "ignore"))
    return [value for value in found if len(value) >= 3]


def main():
    parser = argparse.ArgumentParser(description="Package the Release build for a GitHub release.")
    parser.add_argument("version", help="for example 0.2")
    parser.add_argument("--dirty", action="store_true", help="allow uncommitted changes")
    options = parser.parse_args()

    if not re.fullmatch(r"[0-9A-Za-z.\-]+", options.version):
        sys.exit("A version is made of letters, digits, dots and dashes.")
    for source in CONTENTS.values():
        if not os.path.isfile(source):
            sys.exit("Missing %s. Run tools\\build_release.bat first." % source)
    uncommitted = git("status", "--short")
    if uncommitted and not options.dirty:
        sys.exit("There are uncommitted changes, so the zip would not match a commit:\n%s\n"
                 "Commit them first, or pass --dirty for a trial run." % uncommitted)
    newest_source = max(os.path.getmtime(os.path.join(folder, name))
                        for folder in (os.path.join(ROOT, "core"), os.path.join(ROOT, "mod"))
                        for name in os.listdir(folder) if name.endswith((".c", ".cpp", ".h")))
    if os.path.getmtime(CONTENTS["SlayTheScarlet.dll"]) < newest_source:
        sys.exit("The DLL is older than the source. Run tools\\build_release.bat again.")

    name = "SlayTheScarlet-" + options.version
    release = os.path.join(ROOT, "release")
    debug = os.path.join(release, name + "-debug")
    os.makedirs(debug, exist_ok=True)
    readme = zip_readme()

    path = os.path.join(release, name + ".zip")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for inside, source in CONTENTS.items():
            data = readme if inside == "README.md" else open(source, "rb").read()
            archive.writestr("SlayTheScarlet/" + inside, data)

    shutil.copy2(os.path.join(BUILD, "SlayTheScarlet.pdb"), debug)
    shutil.copy2(os.path.join(BUILD, "SlayTheScarlet.dll"), debug)
    with open(os.path.join(debug, "built-from.txt"), "w", newline="\n") as note:
        note.write("commit %s\nuncommitted when built:\n%s\nzip sha256 %s\n"
                   % (git("rev-parse", "--short", "HEAD"), uncommitted or "(none)",
                      hashlib.sha256(open(path, "rb").read()).hexdigest()))

    print("%s, %.1f KB" % (path, os.path.getsize(path) / 1024))
    problems = 0
    with zipfile.ZipFile(path) as archive:
        if archive.testzip():
            sys.exit("The zip is damaged.")
        for info in archive.infolist():
            data = archive.read(info)
            inside = info.filename[len("SlayTheScarlet/"):]
            expected = readme if inside == "README.md" else open(CONTENTS[inside], "rb").read()
            same = data == expected
            print("  %-36s %8d bytes  %s" % (info.filename, info.file_size, "matches" if same else "DIFFERENT"))
            problems += not same
            for needle in private_strings(inside):
                if needle.lower() in data.lower():
                    print("  PRIVATE: %s contains %r" % (info.filename, needle))
                    problems += 1
            if inside in UTF8_TEXT:
                try:
                    data.decode("utf-8")
                except UnicodeDecodeError:
                    print("  NOT UTF-8: %s" % info.filename)
                    problems += 1
            elif not inside.endswith(".dll") and any(byte > 127 for byte in data):
                print("  NOT ASCII: %s" % info.filename)
                problems += 1
    if problems:
        os.remove(path)
        sys.exit("%d problem(s), so the zip was deleted." % problems)
    print("Checked: contents match, ASCII text (UTF-8 translations), nothing private. The debug files are in %s" % debug)


main()
