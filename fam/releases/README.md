# Packages of the releases

The `.fwsc` package attached to each GitHub release of FM-FAM, under the same name (`fm-fam-VERSION.fwsc`).

The screen mirror page (`fam/docs/FM-FAM-mirror.html`, section Install a version) installs a release from here: a browser cannot
download the file attached to a release (GitHub sends no CORS headers for it), but it can read this repository through
GitHub's API. The page lists the releases from the API and writes a copy only if its SHA-256 is the one GitHub shows for the
release's own file, so a copy that is not that file is never written.

`fam/release.sh` puts the package of the version in `fam/app.h` here; commit it, and attach the same file to the release.
