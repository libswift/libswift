if [ -e ~/.building_swift ]; then
    exit 0
fi

touch ~/.building_swift

cd swift || exit 1
if [ ! -d bin ]; then mkdir bin; fi
git pull origin $BRANCH:$BRANCH || exit 2
rm bin/swift-pg bin/swift-o3 bin/swift-dbg

g++ -I. *.cpp ext/seq_picker.cpp -pg -o bin/swift-pg &
g++ -I. *.cpp ext/seq_picker.cpp -g -o bin/swift-dbg &
g++ -I. *.cpp ext/seq_picker.cpp -O3 -o bin/swift-o3 &
wait
if [ ! -e bin/swift-pg ]; then exit 4; fi
if [ ! -e bin/swift-dbg ]; then exit 5; fi
if [ ! -e bin/swift-o3 ]; then exit 6; fi

rm ~/.building_swift
