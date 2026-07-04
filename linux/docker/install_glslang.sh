#!/usr/bin/env bash
# Bake glslang 14.3.0 + NHL P-3 SpvBuilder patch into /opt/glslang.
set -euo pipefail

git clone --depth 1 --branch 14.3.0 \
  https://github.com/KhronosGroup/glslang.git /opt/glslang

python3 - <<'PY'
from pathlib import Path
p = Path("/opt/glslang/SPIRV/SpvBuilder.cpp")
t = p.read_text()
marker = "nhl high-cut P-3"
if marker not in t:
    old = "    Id typeId = getResultingAccessChainType();"
    new = (
        "    Id typeId = getTypeId(base); /* nhl high-cut P-3: stateless access-chain type */\n"
        "    typeId = getContainedTypeId(typeId);\n"
        "    for (int nhl_i = 0; nhl_i < (int)offsets.size(); ++nhl_i)\n"
        "        typeId = isStructType(typeId) ? getContainedTypeId(typeId, getConstantScalar(offsets[nhl_i]))\n"
        "                                      : getContainedTypeId(typeId, offsets[nhl_i]);"
    )
    if old not in t:
        raise SystemExit("glslang P-3 patch site not found")
    p.write_text(t.replace(old, new, 1))
print("glslang P-3 patch applied")
PY

chmod -R a+rX /opt/glslang
test -f /opt/glslang/SPIRV/SpvBuilder.h
