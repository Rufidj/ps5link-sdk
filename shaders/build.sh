#!/bin/bash
# Assembles every pixel program in src/ and packs each into a shader container
# the console accepts, plus a C header holding its bytes.
#
# The container comes from SharpProspero's mesh pixel program
# (third_party/sharpprospero/mesh_ps.sb, GPL-3.0): "agcpack.py
# texture-container" declares in it one texture and one sampler, which is what
# a program that samples a texture needs. The vertex program is SharpProspero's
# mesh_vs.sb as it is.
#
# Needs llvm-mc and llvm-objcopy built with the amdgcn target (Ubuntu's LLVM 18
# has them); LLVM_MC and LLVM_OBJCOPY name them if they are not on the PATH.
#
#   ./build.sh [VGPRS]     VGPRS defaults to 4: how many vector registers the
#                          program declares. 2 covers v0-v10 only.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build
third=$here/third_party/sharpprospero
LLVM_MC=${LLVM_MC:-llvm-mc}
LLVM_OBJCOPY=${LLVM_OBJCOPY:-llvm-objcopy}
vgprs=${1:-4}
mkdir -p "$out"

python3 "$here/tools/agcpack.py" texture-container "$third/mesh_ps.sb" "$out/texture_container.sb"
python3 "$here/tools/agcpack.py" array "$out/mesh_vs_sb.h" mesh_vs_sb "$third/mesh_vs.sb"

for source in "$here"/src/*.s; do
    name=$(basename "$source" .s)
    "$LLVM_MC" -triple=amdgcn-amd-amdhsa -mcpu=gfx1030 -filetype=obj -o "$out/$name.o" "$source"
    "$LLVM_OBJCOPY" --dump-section .text="$out/$name.bin" "$out/$name.o" "$out/$name.stripped.o"
    python3 "$here/tools/agcpack.py" pack "$out/texture_container.sb" "$out/$name.bin" "$out/$name.sb" --vgprs "$vgprs"
    python3 "$here/tools/agcpack.py" array "$out/${name}_sb.h" "${name}_sb" "$out/$name.sb"
    echo "$name: $(stat -c%s "$out/$name.bin") bytes of program -> $out/${name}_sb.h"
done
