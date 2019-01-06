#!/bin/sh
# SPDX-License-Identifier: MIT

OLD_VERSION=${1:-}
NEW_VERSION=${2:-}

usage() {
    echo "$0: OLD_VERSION NEW_VERSION"
    echo ""
    echo "example:"
    echo "  $0 0.1 0.2"
}

if [ -z "$OLD_VERSION" ] || [ -z "$NEW_VERSION" ] ; then
    usage
    exit 1
fi

echo "$NEW_VERSION" > newchangelog
git shortlog "$OLD_VERSION".. >> newchangelog
cat CHANGELOG.md >> newchangelog

emacs newchangelog --eval "(text-mode)"

echo -n "All fine, ready to release? [y/N]"
read a
a=$(echo "$a" | tr '[:upper:]' '[:lower:]')
if [ "$a" != "y" ]; then
    echo "no not happy, let's stop doing the release"
    exit 1
fi

mv newchangelog CHANGELOG.md
sed -i "s,\(__version__ =\).*,\1 \'$NEW_VERSION\'," jitterplot
sed -i "s,\(#define JD_VERSION\).*,\1 \"$NEW_VERSION\"," jitterdebugger.h

git add CHANGELOG.md jitterplot jitterdebugger.h

git commit -m "Release $NEW_VERSION"
git tag -s -m "Release $NEW_VERSION" "$NEW_VERSION"
git push --follow-tags
