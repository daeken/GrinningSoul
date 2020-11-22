#!/bin/bash

echo "Converting $1"
python wrap.py "$1" "convertedApps/$(basename "$1")";
#xcrun simctl install 4AE7240F-67C1-4197-9639-235C2E6417A1 "convertedApps/$(basename "$1")";
xcrun simctl install D1A6DE6D-4268-4FC5-8BD2-6083F1E3BD33 "convertedApps/$(basename "$1")";
#xcrun simctl install 5FF1783B-C870-4688-8D3D-8B8CC6A818C0 "convertedApps/$(basename "$1")";