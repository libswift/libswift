#if [ $EMIF ]; then
#    sudo tc qdisc del dev $EMIF ingress
#    sudo tc qdisc del dev ifb0 root
#fi
#sudo iptables -F &
cd swift
rm -rf *chunk core *harvest ~/.building_swift ~/.dohrv_copying
killall swift-o2
killall swift-dbg
echo DONE
