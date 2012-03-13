if ps -ef | grep l[e]echer > /dev/null; then
    echo `hostname` has a running leecher
    return 1
fi
