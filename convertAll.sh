#!/bin/bash

mkdir convertedApps 2>/dev/null

for fn in ~/ios\ apps/*.app; do
  echo $fn;
  python wrap.py "$fn" "convertedApps/$(basename "$fn")";
  #xcrun simctl install 24AFE712-AC84-4A97-A1E8-EBB7137E4367 "convertedApps/$(basename "$fn")";
  xcrun simctl install D1A6DE6D-4268-4FC5-8BD2-6083F1E3BD33 "convertedApps/$(basename "$fn")";
  #xcrun simctl install 5FF1783B-C870-4688-8D3D-8B8CC6A818C0 "convertedApps/$(basename "$fn")";
done

