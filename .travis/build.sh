#!/bin/sh -e

TMP="$1"
OUT="$2"

make

mkdir -p "${TMP}/toswin2"

make PREFIX="${TMP}/toswin2" install

find "${TMP}" -type f -perm -a=x -exec m68k-atari-mint-strip -s {} \;
find "${TMP}" -type f \( -name '*.a' -o -name '*.o' \) -exec m68k-atari-mint-strip -S -X -w -N '.L[0-9]*' {} \;
mkdir -p "${OUT}" && cd "${TMP}" && tar cjf "${OUT}/${PROJECT}-${SHORT_ID}.tar.bz2" *
