#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
fail() { echo "[release] $*" >&2; exit 1; }
[[ $(git symbolic-ref --quiet --short HEAD) == main ]] || fail 'Switch to main before publishing.'
[[ -z $(git status --porcelain --untracked-files=normal) ]] || fail 'Commit your changes first; publishing never auto-commits.'
version=$(awk '/^APP_VERSION[[:space:]]*:=/ {print $3; exit}' makefile | tr -d '\r')
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.-]+)?$ ]] || fail 'Invalid APP_VERSION in makefile.'
tag="v$version"
[[ $(head -n 1 .github/release-notes.md | tr -d '\r') == "# NX-Cast $tag Release Notes" ]] || fail 'Update .github/release-notes.md for this version first.'
git remote get-url origin >/dev/null || fail 'Configure the GitHub origin remote first.'
if git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null; then
    fail "$tag already exists remotely. Increase APP_VERSION; existing releases are never overwritten."
else
    status=$?
    [[ $status == 2 ]] || fail 'Cannot query origin tags. Check network and Git credentials.'
fi
if git show-ref --verify --quiet "refs/tags/$tag"; then
    [[ $(git rev-parse "$tag^{commit}") == $(git rev-parse HEAD) ]] || fail "Local $tag points to a different commit."
else
    git tag -a "$tag" -m "NX-Cast $version"
fi
# One transaction: no orphan release tag if main is rejected as non-fast-forward.
git push --atomic origin HEAD:refs/heads/main "refs/tags/$tag:refs/tags/$tag"
echo "[release] Pushed main and $tag. GitHub Actions will build and publish NX-Cast-sdmc.zip."
echo '[release] Push success is not build success; check the Release workflow on GitHub.'
