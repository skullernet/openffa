#!/bin/sh

# check if -fvisibility is supported by GCC
tmpc="/tmp/openffa-${RANDOM}.c"
tmpo="/tmp/openffa-${RANDOM}.o"

echo "int main(){return 0;}" > $tmpc
if gcc -o $tmpo -fvisibility\=hidden $tmpc 2>/dev/null ; then
    echo "-fvisibility=hidden"
fi
rm -f $tmpc $tmpo
