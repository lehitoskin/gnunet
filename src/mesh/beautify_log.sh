#!/bin/sh
cp log .log
cat .log | grep "STARTING SERVICE " > __tmp_peers
cat __tmp_peers | while read line; do
    PEER=`echo $line | sed -e 's/.*\[\(....\)\].*/\1/'`
    PID=`echo $line | sed -e 's/.*mesh-\([0-9]*\).*/\1/'`
    echo "$PID => $PEER"
    cat .log | sed -e "s/mesh-\([a-z2]*\)-$PID/MESH \1 $PEER/" > .log2
    cat .log2 | sed -e "s/mesh-$PID/MESH XXX $PEER/" > .log3
    mv .log3 .log
done 

rm __tmp_peers

cat .log | sed -e 's/mesh-api-/mesh-api-                                            /g' > .log2
mv .log2 .log

if [[ "`ps aux | grep "kwrite .lo[g]"`" = "" ]]; then
    kwrite .log --geometry 960x1140-960 &
fi
