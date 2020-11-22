#!/bin/bash

pushd /Users/daeken/projects/TrampolineTests > /dev/null
xcodebuild -configuration Debug -target TrampolineTests -arch x86_64 -sdk iphonesimulator13.2 > /dev/null
# /Users/daeken/projects/TrampolineTests/build/Debug-iphonesimulator/TrampolineTests.app
xcodebuild -configuration Debug -target TrampolineTests -arch arm64 -sdk iphoneos13.2 > /dev/null
# /Users/daeken/projects/TrampolineTests/build/Debug-iphoneos/TrampolineTests.app

popd > /dev/null

rm -rf TrampolineTests.app 2>/dev/null
cp -rf /Users/daeken/projects/TrampolineTests/build/Debug-iphoneos/TrampolineTests.app TrampolineTestsArm.app
rm -rf TrampolineTestsArm.app/Frameworks/NativeSide.framework
cp -rf /Users/daeken/projects/TrampolineTests/build/Debug-iphonesimulator/TrampolineTests.app/Frameworks/NativeSide.framework TrampolineTestsArm.app/Frameworks/
python wrap.py TrampolineTestsArm.app TrampolineTests.app
rm -rf TrampolineTestsArm.app 2>/dev/null

xcrun simctl install 4AE7240F-67C1-4197-9639-235C2E6417A1 TrampolineTests.app
rm /Users/daeken/emulog.out
PID=`xcrun simctl launch 4AE7240F-67C1-4197-9639-235C2E6417A1 dev.daeken.TrampolineTests | cut -d ' ' -f 2`
lsof -p $PID +r 1 &>/dev/null
cat /Users/daeken/emulog.out | grep -E '~~~|Genuine segfault' | sed 's/^~~~//' | sed 's/^.*segfault.*$/SEGFAULT/' | grep --color -E '^|FAIL|SEGFAULT'
PASSCOUNT=`cat /Users/daeken/emulog.out | fgrep '~~~Pass' | wc -l`
TOTALCOUNT=`cat /Users/daeken/emulog.out | grep -E '~~~Pass|~~~FAIL' | wc -l`
FAILCOUNT=`cat /Users/daeken/emulog.out | grep -E '~~~FAIL|Genuine segfault' | sed 's/^~~~//' | sed 's/^.*segfault.*$/SEGFAULT/' | wc -l`
if [ "$FAILCOUNT" -ne "0" ]; then
	echo
	echo 'Failures:'
	cat /Users/daeken/emulog.out | grep -E '~~~FAIL|Genuine segfault' | sed 's/^~~~//' | sed 's/^.*segfault.*$/SEGFAULT/' | grep --color -E '^|FAIL|SEGFAULT';
fi
echo `echo $PASSCOUNT | sed -e 's/^[[:space:]]*//'`/`echo $TOTALCOUNT | sed -e 's/^[[:space:]]*//'` passing