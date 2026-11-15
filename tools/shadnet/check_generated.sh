#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 GR2toXBOX contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Checks the checked-in protobuf sources are the ones the vendored .proto and
# runtime imply, without needing protoc.
#
# Two things can go wrong now that they are committed rather than generated: the
# .proto can change without the generated files being regenerated, and the
# vendored protobuf can be updated without them being regenerated. Neither shows
# up until a Windows runner compiles the result, and the second shows up as an
# #error in generated code, which reads like a broken checkout rather than a
# stale file. Both are checkable here.

set -uo pipefail

cd "$(dirname "$0")/../.." || exit 1

fail=0

# 1. The generated pair exists.
for file in src/shadnet/generated/shadnet.pb.h src/shadnet/generated/shadnet.pb.cc; do
    if [ ! -f "$file" ]; then
        echo "check_generated: $file is missing (tools/shadnet/regenerate.sh)" >&2
        fail=1
    fi
done
[ "$fail" -ne 0 ] && exit 1

# 2. The version guard matches the vendored runtime.
guard=$(grep -o 'PROTOBUF_VERSION != [0-9]*' src/shadnet/generated/shadnet.pb.h | head -1 | awk '{print $3}')
runtime=$(sed -n 's/^#define PROTOBUF_OSS_VERSION \([0-9]*\)$/\1/p' \
    externals/protobuf/src/google/protobuf/runtime_version.h | head -1)
if [ -z "$guard" ] || [ -z "$runtime" ]; then
    echo "check_generated: could not read the version guard or the runtime version." >&2
    fail=1
elif [ "$guard" != "$runtime" ]; then
    echo "check_generated: generated for $guard but the vendored runtime is $runtime." >&2
    echo "  Regenerate: tools/shadnet/regenerate.sh <protoc matching that version>" >&2
    fail=1
fi

# 3. The .proto has not changed since the generated file was made. protoc writes
#    the source path into the header, not a hash, so the file's mtime is the
#    signal available: if the .proto is newer than the generated pair, the pair
#    predates an edit. Coarse, and it only works from a real checkout (a fresh
#    clone has every file at checkout time), so it is a warning rather than a
#    failure -- the version guard above is the hard check.
if [ src/shadnet/shadnet.proto -nt src/shadnet/generated/shadnet.pb.h ]; then
    echo "check_generated: shadnet.proto is newer than the generated sources." >&2
    echo "  If you edited it, run tools/shadnet/regenerate.sh. A fresh clone trips this." >&2
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "check_generated: OK (PROTOBUF_VERSION $guard)"
