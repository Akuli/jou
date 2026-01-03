#!/bin/bash
set -e

make

rm -rf tmp/cachebug
mkdir -vp tmp/cachebug

# Compile exe1 from src1 with no cache
cp -r src1 tmp/cachebug/src
./jou -o tmp/cachebug/exe1 tmp/cachebug/src/main.jou

# Keep cache, compile exe2 from src2
shopt -s extglob
rm -rf tmp/cachebug/src/!(jou_compiled)
cp -r src2/* tmp/cachebug/src/
./jou -o tmp/cachebug/exe2 tmp/cachebug/src/main.jou

# Clear cache, compile exe3 from src2
rm -rf tmp/cachebug/src/jou_compiled/
./jou -o tmp/cachebug/exe3 tmp/cachebug/src/main.jou

# Now exe2 and exe3 should be the same. We have a bug if they differ.
md5sum tmp/cachebug/exe{1,2,3}
if ! diff tmp/cachebug/exe{2,3}; then
    echo "CACHE BUG FOUND!!!!!!"
    git add src1 src2
    git commit -m "Reduce cache bug with $0"
fi
