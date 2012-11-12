#!/bin/bash

for tst in `ls tests/*test | grep -v ledbat`; do
    if echo $tst; $tst > $tst.log; then
        echo $tst OK
    else
        echo $tst FAIL
    fi
done
