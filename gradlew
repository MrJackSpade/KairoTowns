#!/usr/bin/env sh
set -eu
kairo_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -f "$kairo_root/shared/gradle/wrapper/gradle-wrapper.jar" ]; then
  echo "Initialize the pinned shared project: git submodule update --init shared" >&2
  exit 1
fi
exec "$kairo_root/shared/gradlew" -p "$kairo_root" "$@"
