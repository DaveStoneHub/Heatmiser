#!/bin/bash
# Temporary: find which esp_matter resolves on this IDF and print the API we use.
cd tools/matter_probe
idf.py set-target esp32s3 >/dev/null 2>&1 || true
idf.py reconfigure >/dev/null 2>&1 || true
grep -A3 "espressif/esp_matter:" dependencies.lock
M=managed_components/espressif__esp_matter
find $M -name "esp_matter_*.h" -path "*esp_matter*" | grep -v connectedhomeip | head -40
show() { # file pattern lines
  f=$(find $M -name "$1" | grep -v connectedhomeip | head -1)
  echo "=== $1 /$2/"
  grep -n -A"$3" -E "$2" "$f" | head -400
}
show esp_matter_endpoint.h "namespace (thermostat|bridged_node|aggregator) \{" 25
show esp_matter_cluster.h "namespace (thermostat|bridged_device_basic_information|thermostat_user_interface_configuration) \{" 45
show esp_matter_core.h "resume|set_parent_endpoint|endpoint_t \*create|esp_err_t enable|esp_err_t destroy|ENDPOINT_FLAG|callback_t\)|typedef esp_err_t \(\*callback_t|namespace lock|chip_stack_lock|esp_err_t start|uint16_t get_id" 3
show esp_matter_attribute.h "namespace (thermostat|bridged_device_basic_information|thermostat_user_interface_configuration) \{" 60
show esp_matter_feature.h "namespace thermostat \{" 60
show esp_matter_attribute_utils.h "esp_matter_(nullable_int16|nullable_uint16|enum8|bitmap16|char_str|bool|int16|uint8)\(|esp_err_t update|PRE_UPDATE" 2
show esp_matter.h "include" 0
grep -rn "struct config" $M/components/esp_matter/data_model/esp_matter_data_model.h 2>/dev/null | head
grep -rln "namespace root_node" $M/components | head
