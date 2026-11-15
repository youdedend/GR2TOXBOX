#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Regenerates src/shadnet/generated from src/shadnet/shadnet.proto.
#
# Run this after changing the .proto or updating externals/protobuf. The
# generated header carries a hard equality guard on the runtime's
# PROTOBUF_VERSION, so a protoc that does not match the vendored runtime exactly
# produces a file that will not compile -- which is the point: a mismatch is a
# build error, not a registration failure at runtime.
#
# protoc is not vendored (it is a build tool, not a library, and building it to
# match costs a few minutes). Get one whose version matches
# externals/protobuf/version.json and pass it in:
#
#   tools/shadnet/regenerate.sh /path/to/protoc
#
# The version the file was generated with is recorded in its header comment.

set -euo pipefail

cd "$(dirname "$0")/../.." || exit 1

PROTOC=${1:-protoc}
if ! command -v "$PROTOC" >/dev/null 2>&1; then
    echo "regenerate: '$PROTOC' is not executable." >&2
    echo "  Build one from externals/protobuf so the versions match exactly:" >&2
    echo "    cmake -S externals/protobuf -B build-protoc -Dprotobuf_BUILD_TESTS=OFF \\" >&2
    echo "          -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_ABSL_PROVIDER=module" >&2
    echo "    cmake --build build-protoc --target protoc --parallel" >&2
    exit 1
fi

echo "regenerate: $("$PROTOC" --version)"
"$PROTOC" -I src/shadnet --cpp_out=src/shadnet/generated src/shadnet/shadnet.proto

# The generated files name the runtime they need; say so, so a mismatch is caught
# here rather than by a Windows runner.
GUARD=$(grep -o 'PROTOBUF_VERSION != [0-9]*' src/shadnet/generated/shadnet.pb.h | head -1 | awk '{print $3}')
RUNTIME=$(sed -n 's/^#define PROTOBUF_OSS_VERSION \([0-9]*\)$/\1/p' \
    externals/protobuf/src/google/protobuf/runtime_version.h | head -1)
echo "regenerate: generated for PROTOBUF_VERSION $GUARD, vendored runtime is $RUNTIME"
if [ "$GUARD" != "$RUNTIME" ]; then
    echo "regenerate: MISMATCH -- these files will not compile against the vendored runtime." >&2
    exit 1
fi
echo "regenerate: src/shadnet/generated is up to date"
