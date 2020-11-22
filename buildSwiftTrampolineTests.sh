#!/bin/bash

#####pushd /Users/daeken/projects/SwiftTrampolineTests > /dev/null
#####xcodebuild -configuration Debug -target SwiftTrampolineTests -arch x86_64 -sdk iphonesimulator13.2 > /dev/null
###### /Users/daeken/Library/Developer/Xcode/DerivedData/SwiftTrampolineTests-bokcuymepnhrwddxokpdxjgditxw/Build/Products/Debug-iphonesimulator/SwiftTrampolineTests.app
#####xcodebuild -configuration Debug -target SwiftTrampolineTests -arch arm64 -sdk iphoneos13.2 > /dev/null
###### /Users/daeken/Library/Developer/Xcode/DerivedData/SwiftTrampolineTests-bokcuymepnhrwddxokpdxjgditxw/Build/Products/Debug-iphoneos/SwiftTrampolineTests.app
#####
#####popd > /dev/null
#####
#####rm -rf SwiftTrampolineTests.app 2>/dev/null
#####cp -rf /Users/daeken/Library/Developer/Xcode/DerivedData/SwiftTrampolineTests-bokcuymepnhrwddxokpdxjgditxw/Build/Products/Debug-iphoneos/SwiftTrampolineTests.app SwiftTrampolineTestsArm.app
#####rm -rf SwiftTrampolineTestsArm.app/Frameworks/NativeSide.framework
#####cp -rf /Users/daeken/Library/Developer/Xcode/DerivedData/SwiftTrampolineTests-bokcuymepnhrwddxokpdxjgditxw/Build/Products/Debug-iphonesimulator/SwiftTrampolineTests.app/Frameworks/NativeSide.framework SwiftTrampolineTestsArm.app/Frameworks/
#####python wrap.py SwiftTrampolineTestsArm.app SwiftTrampolineTests.app
#####rm -rf SwiftTrampolineTestsArm.app 2>/dev/null
#####
#####xcrun simctl install 4AE7240F-67C1-4197-9639-235C2E6417A1 SwiftTrampolineTests.app
######xcrun simctl install 5FF1783B-C870-4688-8D3D-8B8CC6A818C0 SwiftTrampolineTests.app
#####
rm /Users/daeken/emulog.out
PID=`xcrun simctl launch 4AE7240F-67C1-4197-9639-235C2E6417A1 dev.daeken.SwiftTrampolineTests | cut -d ' ' -f 2`
#PID=`xcrun simctl launch 5FF1783B-C870-4688-8D3D-8B8CC6A818C0 dev.daeken.SwiftTrampolineTests | cut -d ' ' -f 2`
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