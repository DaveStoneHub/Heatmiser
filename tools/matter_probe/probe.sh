#!/bin/bash
# Temporary: find which esp_matter resolves on this IDF and print its API.
cd tools/matter_probe
set -x
idf.py set-target esp32s3 || true
idf.py reconfigure || true
cat dependencies.lock || true
M=$(ls -d managed_components/espressif__esp_matter 2>/dev/null)
echo "=== MANIFEST"; cat $M/idf_component.yml
ls $M $M/esp_matter $M/connectedhomeip 2>/dev/null | head -80
for f in esp_matter_core.h esp_matter_endpoint.h esp_matter_cluster.h esp_matter_attribute.h esp_matter_feature.h esp_matter.h esp_matter_attribute_utils.h esp_matter_command.h; do
  p=$(find $M -name $f | head -1); echo "=== FILE $p"; cat "$p"
done 2>&1 | grep -nE "^=== |namespace (thermostat|aggregator|bridged_node|bridged_device_basic_information|identify|thermostat_user_interface_configuration|node|endpoint|on_off_plugin_unit|on_off) |resume|create\(|add\(|config\(|struct config|feature_flags|features|enable\(|destroy|set_parent|ENDPOINT_FLAG|typedef .*callback|attribute::update|esp_err_t update|report\(|get_val|set_val|get_id|node_label|reachable|setpoint|system_mode|running_state|local_temperature|keypad|control_sequence|heating|delegate|lock\b|lock_" | head -1500
echo "=== BUILD"
idf.py build 2>&1 | tail -150
