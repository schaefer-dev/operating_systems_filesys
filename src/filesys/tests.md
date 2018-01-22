# filesys/extended/grow-seq-lg
pintos -v -k -T 60 --qemu  --disk=tmp.dsk -p tests/filesys/extended/grow-seq-lg -a grow-seq-lg -p tests/filesys/extended/tar -a tar -- -q  -f run grow-seq-lg < /dev/null 2> tests/filesys/extended/grow-seq-lg.errors > tests/filesys/extended/grow-seq-lg.output
