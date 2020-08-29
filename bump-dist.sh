#! /bin/bash

# Copyright (c) 2020 Albert Gräf <aggraef@gmail.com>. Distributed under the
# MIT license, please check the toplevel LICENSE file for details.

# Bump the appveyor version if the version given in src/faustgen_tilde.c has
# changed.
version=$(grep "#define FAUSTGEN_VERSION_STR" src/faustgen_tilde.c | sed 's|^#define *FAUSTGEN_VERSION_STR *"\(.*\)".*|\1|')
avversion=$(grep "^version: " appveyor.yml | sed 's|^version: *\(.*\).*|\1|' | sed 's|[.]{build}$||')

echo "version: $version, appveyor: $avversion"

if [ "$version" != "$apversion" ]; then
    sed -i -e "s|^version:.*|version: $version.{build}|" appveyor.yml
fi
