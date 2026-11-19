#!/bin/bash
# Temporary: find which esp_matter resolves on this IDF and print the API we use.
cd tools/matter_probe
idf.py set-target esp32s3 >/dev/null 2>&1 || true
idf.py reconfigure >/dev/null 2>&1 || true
grep -A12 "espressif/esp_matter:" dependencies.lock | grep -E "version|esp_matter:"
M=managed_components/espressif__esp_matter/components/esp_matter
G=$M/data_model/generated
ls $G $G/device_types $G/clusters 2>/dev/null | tr '\n' ' '
echo
for d in $(find $G -type d \( -iname "*thermostat*" -o -iname "*bridged*" -o -iname "*aggregator*" \)); do
  for f in $d/*.h; do echo "=== FILE $f"; grep -v '^ *//\|^ \*\|^/\*\|^$' "$f"; done
done
for f in $M/esp_matter_core.h $M/data_model/esp_matter_data_model.h $(find $M -name esp_matter_endpoint.h) $M/data_model/esp_matter_cluster.h; do
  echo "=== FILE $f"; grep -v '^ *\*\|^ */\*\|^$\|^ *//' "$f" | head -400
done
grep -rn "create_unique_id\|create_node_label" $M | head
grep -rn "OnboardingCodesUtil\|GetQRCode" managed_components/espressif__esp_matter/connectedhomeip/connectedhomeip/src/app/server/OnboardingCodesUtil.h | head
