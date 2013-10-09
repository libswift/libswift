#!/bin/bash

#for tst in `ls tests/*_bug | grep -v ledbat`; do
#    if echo $tst; $tst > $tst.log; then
#        echo $tst OK
#    else
#        echo $tst FAIL
#    fi
#done

#!/bin/bash

for tst in tests/*; do
    if [ -x $tst ]; then
        echo $tst 
        $tst
    fi  
done

