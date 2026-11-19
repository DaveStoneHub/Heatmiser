/*
 * Matter bridge: root node, Aggregator, and one bridged Thermostat endpoint
 * per zone (design doc, "Matter bridge endpoint model").
 *
 * Threads:
 * - The CHIP task calls attr_cb for every attribute change. A change from a
 *   controller is checked and queued on the bus through the zone store,
 *   which never blocks.
 * - The "mt_sync" task takes the zone store's Matter dirty bits twice a
 *   second, adds or removes endpoints when the zone list changes, and
 *   pushes changed values into Matter under the CHIP stack lock.
 */
#include "matter_bridge.h"

#include <stdio.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_matter.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if __has_include(<setup_payload/OnboardingCodesUtil.h>)
#include <setup_payload/OnboardingCodesUtil.h>
#else
#include <app/server/OnboardingCodesUtil.h>
#endif
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/server/Server.h>
#include <credentials/FabricTable.h>
#include <platform/CHIPDeviceLayer.h>

#include "matter_map.h"
#include "zone_store.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

static const char *TAG = "matter";

#define NZONES 32
#define WINDOW_DEFAULT_S 900

namespace {

struct zone_ep_t {
    uint16_t    id;              /* 0 = no endpoint */
    endpoint_t *ep;
    bool        pushed;          /* last has been sent once */
    mm_thermo_t last;
    char        name[ZONE_NAME_MAX + 1];
};

node_t *s_node;
endpoint_t *s_aggregator;
zone_ep_t s_zone[NZONES];
volatile bool s_running;
volatile int64_t s_window_until_us;  /* when the pairing window closes */
matter_status_t s_status;
portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_local;                    /* we are updating an attribute ourselves */

int zone_of_endpoint(uint16_t id)
{
    for (int i = 0; i < NZONES; i++) {
        if (s_zone[i].id && s_zone[i].id == id) {
            return i;
        }
    }
    return -1;
}

/* ---- Matter -> zone store ------------------------------------------------------ */

esp_err_t zone_result(zone_err_t e, int i, const char *what)
{
    if (e == ZONE_OK) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "zone %d: %s refused: %s", i + 1, what, zone_err_str(e));
    return ESP_FAIL;
}

esp_err_t controller_write(int i, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t *val)
{
    uint8_t addr = (uint8_t)(i + 1);
    if (cluster == Thermostat::Id) {
        switch (attr) {
        case Thermostat::Attributes::OccupiedHeatingSetpoint::Id: {
            int c = mm_setpoint_to_c(val->val.i16);
            ESP_LOGI(TAG, "zone %u: setpoint %d.%02d -> %d C", addr, val->val.i16 / 100, val->val.i16 % 100, c);
            return zone_result(zone_store_set_temp(addr, c), i, "setpoint");
        }
        case Thermostat::Attributes::SystemMode::Id: {
            hm_mode_t m;
            if (!mm_mode_from_system(val->val.u8, &m)) {
                ESP_LOGW(TAG, "zone %u: system mode %u not supported", addr, val->val.u8);
                return ESP_ERR_NOT_SUPPORTED;
            }
            ESP_LOGI(TAG, "zone %u: system mode %u -> %s", addr, val->val.u8, hm_mode_str(m));
            return zone_result(zone_store_set_mode(addr, m), i, "mode");
        }
        default:
            ESP_LOGW(TAG, "zone %u: thermostat attribute 0x%04lx is read only here", addr, (unsigned long)attr);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (cluster == ThermostatUserInterfaceConfiguration::Id &&
        attr == ThermostatUserInterfaceConfiguration::Attributes::KeypadLockout::Id) {
        ESP_LOGI(TAG, "zone %u: keypad lockout %u", addr, val->val.u8);
        return zone_result(zone_store_set_lock(addr, val->val.u8 != 0), i, "key lock");
    }
    if (cluster == ThermostatUserInterfaceConfiguration::Id) {
        return ESP_ERR_NOT_SUPPORTED; /* display mode is fixed by the stat */
    }
    return ESP_OK; /* identify and the rest: let the stack handle them */
}

esp_err_t attr_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv)
{
    (void)priv;
    if (type != attribute::PRE_UPDATE || s_local) {
        return ESP_OK;
    }
    int i = zone_of_endpoint(endpoint_id);
    if (i < 0) {
        return ESP_OK;
    }
    return controller_write(i, cluster_id, attribute_id, val);
}

esp_err_t identify_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                      uint8_t effect_variant, void *priv)
{
    (void)effect_id;
    (void)effect_variant;
    (void)priv;
    ESP_LOGI(TAG, "identify endpoint %u (%d)", endpoint_id, (int)type);
    return ESP_OK;
}

void event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "commissioning failed: fail-safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        if (s_window_until_us < esp_timer_get_time()) {
            /* opened by the stack at boot, for the default discovery time */
            s_window_until_us = esp_timer_get_time() + (int64_t)WINDOW_DEFAULT_S * 1000000;
        }
        ESP_LOGI(TAG, "pairing window open");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        s_window_until_us = 0;
        ESP_LOGI(TAG, "pairing window closed");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "a controller removed the bridge");
        break;
    default:
        break;
    }
}

/* ---- zone store -> Matter (CHIP stack lock held) --------------------------------- */

void put_val(uint16_t ep, uint32_t cluster, uint32_t attr, esp_matter_attr_val_t *v)
{
    s_local = true;
    esp_err_t e = attribute::update(ep, cluster, attr, v);
    s_local = false;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "endpoint %u: update 0x%04lx/0x%04lx failed: %s", ep, (unsigned long)cluster,
                 (unsigned long)attr, esp_err_to_name(e));
    }
}

void push(int i, const zone_view_t &v)
{
    zone_ep_t &z = s_zone[i];
    mm_thermo_t m;
    mm_from_view(&v, &m);
    const mm_thermo_t &o = z.last;
    bool all = !z.pushed;
    uint16_t ep = z.id;

    if (all || m.reachable != o.reachable) {
        esp_matter_attr_val_t a = esp_matter_bool(m.reachable);
        put_val(ep, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id, &a);
    }
    if (all || strcmp(z.name, v.cfg.name)) {
        snprintf(z.name, sizeof z.name, "%s", v.cfg.name);
        esp_matter_attr_val_t a = esp_matter_char_str(z.name, (uint16_t)strlen(z.name));
        put_val(ep, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, &a);
    }
    if (all || m.local_null != o.local_null || m.local_t != o.local_t) {
        esp_matter_attr_val_t a = m.local_null ? esp_matter_nullable_int16(nullable<int16_t>())
                                               : esp_matter_nullable_int16(m.local_t);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id, &a);
    }
    if (all || m.heat_setpoint != o.heat_setpoint) {
        esp_matter_attr_val_t a = esp_matter_int16(m.heat_setpoint);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id, &a);
    }
    if (all || m.system_mode != o.system_mode) {
        esp_matter_attr_val_t a = esp_matter_enum8(m.system_mode);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::SystemMode::Id, &a);
    }
    if (all || m.running_state != o.running_state) {
        esp_matter_attr_val_t a = esp_matter_bitmap16(m.running_state);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::ThermostatRunningState::Id, &a);
        esp_matter_attr_val_t d = esp_matter_uint8(m.pi_heating);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::PIHeatingDemand::Id, &d);
    }
    if (all || m.hold != o.hold || m.hold_min != o.hold_min) {
        esp_matter_attr_val_t h = esp_matter_enum8(m.hold);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::TemperatureSetpointHold::Id, &h);
        esp_matter_attr_val_t d = m.hold_null ? esp_matter_nullable_uint16(nullable<uint16_t>())
                                              : esp_matter_nullable_uint16(m.hold_min);
        put_val(ep, Thermostat::Id, Thermostat::Attributes::TemperatureSetpointHoldDuration::Id, &d);
    }
    if (all || m.keypad_lockout != o.keypad_lockout) {
        esp_matter_attr_val_t a = esp_matter_enum8(m.keypad_lockout);
        put_val(ep, ThermostatUserInterfaceConfiguration::Id,
                ThermostatUserInterfaceConfiguration::Attributes::KeypadLockout::Id, &a);
    }
    if (all || m.display_mode != o.display_mode) {
        esp_matter_attr_val_t a = esp_matter_enum8(m.display_mode);
        put_val(ep, ThermostatUserInterfaceConfiguration::Id,
                ThermostatUserInterfaceConfiguration::Attributes::TemperatureDisplayMode::Id, &a);
    }
    z.last = m;
    z.pushed = true;
}

/* Optional attributes the bridge reports, made only if the data model has
 * not made them already. */
void ensure(cluster_t *c, uint32_t attr, attribute_t *(*make)(cluster_t *))
{
    if (c && !attribute::get(c, attr)) {
        make(c);
    }
}

/* Turn a fresh bridged node endpoint into a heat-only thermostat. */
esp_err_t build_thermostat(endpoint_t *ep, const zone_view_t &v)
{
    endpoint::thermostat::config_t tc;
    tc.thermostat.control_sequence_of_operation = MM_CONTROL_HEATING_ONLY;
    tc.thermostat.system_mode = MM_SYSTEM_MODE_HEAT;
    tc.thermostat.feature_flags = cluster::thermostat::feature::heating::get_id();
    tc.thermostat.features.heating.occupied_heating_setpoint = 2000;
    esp_err_t e = endpoint::thermostat::add(ep, &tc);
    if (e != ESP_OK) {
        return e;
    }
    namespace ta = cluster::thermostat::attribute;
    namespace TA = Thermostat::Attributes;
    cluster_t *th = cluster::get(ep, Thermostat::Id);
    ensure(th, TA::AbsMinHeatSetpointLimit::Id, [](cluster_t *c) { return ta::create_abs_min_heat_setpoint_limit(c, MM_SETPOINT_MIN); });
    ensure(th, TA::AbsMaxHeatSetpointLimit::Id, [](cluster_t *c) { return ta::create_abs_max_heat_setpoint_limit(c, MM_SETPOINT_MAX); });
    ensure(th, TA::MinHeatSetpointLimit::Id, [](cluster_t *c) { return ta::create_min_heat_setpoint_limit(c, MM_SETPOINT_MIN); });
    ensure(th, TA::MaxHeatSetpointLimit::Id, [](cluster_t *c) { return ta::create_max_heat_setpoint_limit(c, MM_SETPOINT_MAX); });
    ensure(th, TA::ThermostatRunningState::Id, [](cluster_t *c) { return ta::create_thermostat_running_state(c, 0); });
    ensure(th, TA::PIHeatingDemand::Id, [](cluster_t *c) {
        return attribute::create(c, TA::PIHeatingDemand::Id, ATTRIBUTE_FLAG_NONE, esp_matter_uint8(0));
    });
    ensure(th, TA::TemperatureSetpointHold::Id, [](cluster_t *c) { return ta::create_temperature_setpoint_hold(c, 0); });
    ensure(th, TA::TemperatureSetpointHoldDuration::Id,
           [](cluster_t *c) { return ta::create_temperature_setpoint_hold_duration(c, nullable<uint16_t>()); });

    cluster::thermostat_user_interface_configuration::config_t ui;
    cluster::thermostat_user_interface_configuration::create(ep, &ui, CLUSTER_FLAG_SERVER);

    cluster_t *bdbi = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    if (bdbi && !attribute::get(bdbi, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id)) {
        char name[ZONE_NAME_MAX + 1];
        snprintf(name, sizeof name, "%s", v.cfg.name);
        cluster::bridged_device_basic_information::attribute::create_node_label(bdbi, name, (uint16_t)strlen(name));
    }
    return endpoint::set_parent_endpoint(ep, s_aggregator);
}

/* Give zone i an endpoint, reusing its saved ID. */
void add_zone(int i, const zone_view_t &v)
{
    zone_ep_t &z = s_zone[i];
    const uint8_t flags = ENDPOINT_FLAG_DESTROYABLE | ENDPOINT_FLAG_BRIDGE;
    endpoint::bridged_node::config_t bn;
    bn.bridged_device_basic_information.reachable = v.link != HM_LINK_OFFLINE;
    snprintf(bn.bridged_device_basic_information.unique_id, sizeof bn.bridged_device_basic_information.unique_id,
             "heatmiser-stat-%u", v.cfg.addr);
    endpoint_t *ep = NULL;
    if (v.cfg.endpoint) {
        ep = endpoint::bridged_node::resume(s_node, &bn, flags, v.cfg.endpoint, NULL);
        if (!ep) {
            ESP_LOGW(TAG, "zone %u: endpoint %u could not be restored; making a new one", v.cfg.addr, v.cfg.endpoint);
        }
    }
    if (!ep) {
        ep = endpoint::bridged_node::create(s_node, &bn, flags, NULL);
    }
    if (!ep) {
        ESP_LOGE(TAG, "zone %u: no endpoint (too many?)", v.cfg.addr);
        return;
    }
    if (build_thermostat(ep, v) != ESP_OK || endpoint::enable(ep) != ESP_OK) {
        ESP_LOGE(TAG, "zone %u: thermostat endpoint failed", v.cfg.addr);
        endpoint::destroy(s_node, ep);
        return;
    }
    z.ep = ep;
    z.id = endpoint::get_id(ep);
    z.pushed = false;
    z.name[0] = 0;
    ESP_LOGI(TAG, "zone %u '%s' is endpoint %u", v.cfg.addr, v.cfg.name, z.id);
    if (z.id != v.cfg.endpoint) {
        zone_store_set_endpoint(v.cfg.addr, z.id); /* only touches NVS on a change */
    }
}

void remove_zone(int i)
{
    zone_ep_t &z = s_zone[i];
    if (!z.ep) {
        return;
    }
    ESP_LOGI(TAG, "zone %d removed; endpoint %u retired", i + 1, z.id);
    endpoint::destroy(s_node, z.ep);
    memset(&z, 0, sizeof z);
}

void sync(uint32_t dirty)
{
    uint32_t list = zone_store_list();
    for (int i = 0; i < NZONES; i++) {
        bool want = list & (1u << i);
        zone_view_t v;
        if (want && !zone_store_get((uint8_t)(i + 1), &v)) {
            want = false;
        }
        if (!want) {
            if (s_zone[i].ep) {
                remove_zone(i);
            }
            continue;
        }
        if (!s_zone[i].ep) {
            add_zone(i, v);
            if (!s_zone[i].ep) {
                continue;
            }
        } else if (!(dirty & (1u << i))) {
            continue;
        }
        push(i, v);
    }
}

} // namespace
static void refresh_status(void);
namespace {

void sync_task(void *arg)
{
    (void)arg;
    uint32_t dirty = 0xFFFFFFFFu;
    unsigned n = 0;
    for (;;) {
        dirty |= zone_store_take_dirty_for(ZONE_READER_MATTER);
        {
            lock::ScopedChipStackLock lk(portMAX_DELAY);
            if (dirty) {
                sync(dirty);
                dirty = 0;
            }
            if (++n % 2 == 0) {
                refresh_status();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

} // namespace

/* ---- public ------------------------------------------------------------------------ */

extern "C" esp_err_t matter_bridge_start(void)
{
    node::config_t nc;
    s_node = node::create(&nc, attr_cb, identify_cb);
    if (!s_node) {
        ESP_LOGE(TAG, "node create failed");
        return ESP_FAIL;
    }
    endpoint::aggregator::config_t ac;
    s_aggregator = endpoint::aggregator::create(s_node, &ac, ENDPOINT_FLAG_NONE, NULL);
    if (!s_aggregator) {
        ESP_LOGE(TAG, "aggregator create failed");
        return ESP_FAIL;
    }
    /* esp_matter logs every attribute write at info level: 11 lines per zone
     * at boot and one for every reading. The zone lines say what matters. */
    esp_log_level_set("esp_matter_attribute", ESP_LOG_WARN);
    ESP_LOGI(TAG, "internal RAM before start: %u free, largest block %u, DMA %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    esp_err_t err = esp_matter::start(event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter start failed: %s", esp_err_to_name(err));
        return err;
    }
    s_running = true;

    /* Restore every saved zone's endpoint now, before a controller looks. */
    {
        lock::ScopedChipStackLock lk(portMAX_DELAY);
        sync(0xFFFFFFFFu);
        refresh_status();
    }
    zone_store_take_dirty_for(ZONE_READER_MATTER);
    xTaskCreate(sync_task, "mt_sync", 4096, NULL, 4, NULL);

    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
    return ESP_OK;
}

/* Read under the CHIP stack lock by mt_sync, so the screen never waits on
 * the stack (it can be busy for seconds during pairing). */
static void refresh_status(void)
{
    matter_status_t o;
    memset(&o, 0, sizeof o);
    o.running = true;
    auto &cwm = chip::Server::GetInstance().GetCommissioningWindowManager();
    o.window_open = cwm.IsCommissioningWindowOpen();
    /* The first controller pairs over Bluetooth. Bluetooth is freed once the
     * bridge is on a fabric, so later ones find it on the network. */
    const auto flags = chip::Server::GetInstance().GetFabricTable().FabricCount() == 0
                           ? chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE)
                           : chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kOnNetwork);
    chip::MutableCharSpan qr(o.qr, sizeof o.qr - 1);
    if (GetQRCode(qr, flags) == CHIP_NO_ERROR) {
        o.qr[qr.size()] = 0;
    } else {
        o.qr[0] = 0;
    }
    chip::MutableCharSpan mc(o.manual, sizeof o.manual - 1);
    if (GetManualPairingCode(mc, flags) == CHIP_NO_ERROR) {
        o.manual[mc.size()] = 0;
    } else {
        o.manual[0] = 0;
    }
    for (int i = 0; i < NZONES; i++) {
        o.endpoints += s_zone[i].ep ? 1 : 0;
    }
    for (const auto &fb : chip::Server::GetInstance().GetFabricTable()) {
        if (o.fabric_count >= MATTER_FABRICS_MAX) {
            break;
        }
        matter_fabric_t &f = o.fabrics[o.fabric_count++];
        f.index = fb.GetFabricIndex();
        f.vendor = (uint16_t)fb.GetVendorId();
        chip::CharSpan label = fb.GetFabricLabel();
        size_t n = label.size() < sizeof f.label - 1 ? label.size() : sizeof f.label - 1;
        memcpy(f.label, label.data(), n);
        f.label[n] = 0;
    }
    portENTER_CRITICAL(&s_status_mux);
    s_status = o;
    portEXIT_CRITICAL(&s_status_mux);
}

extern "C" void matter_bridge_status(matter_status_t *o)
{
    portENTER_CRITICAL(&s_status_mux);
    *o = s_status;
    portEXIT_CRITICAL(&s_status_mux);
    int64_t left = (s_window_until_us - esp_timer_get_time()) / 1000000;
    o->window_left_s = o->window_open && left > 0 ? (uint32_t)left : 0;
}

extern "C" esp_err_t matter_bridge_open_window(uint16_t seconds)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    CHIP_ERROR ce = CHIP_NO_ERROR;
    {
        lock::ScopedChipStackLock lk(portMAX_DELAY);
        auto &cwm = chip::Server::GetInstance().GetCommissioningWindowManager();
        if (!cwm.IsCommissioningWindowOpen()) {
            ce = cwm.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(seconds),
                                                  chip::CommissioningWindowAdvertisement::kAllSupported);
    }
    if (ce == CHIP_NO_ERROR) {
        s_window_until_us = esp_timer_get_time() + (int64_t)seconds * 1000000;
        refresh_status();
    }
    }
    if (ce != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "pairing window not opened: %" CHIP_ERROR_FORMAT, ce.Format());
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "pairing window open for %u s", seconds);
    return ESP_OK;
}

extern "C" esp_err_t matter_bridge_remove_fabric(uint8_t index)
{
    if (!s_running) {
        return ESP_ERR_INVALID_STATE;
    }
    CHIP_ERROR ce;
    {
        lock::ScopedChipStackLock lk(portMAX_DELAY);
        ce = chip::Server::GetInstance().GetFabricTable().Delete(index);
        refresh_status();
    }
    if (ce != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "fabric %u not removed: %" CHIP_ERROR_FORMAT, index, ce.Format());
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "fabric %u removed", index);
    return ESP_OK;
}

extern "C" uint16_t matter_bridge_endpoint(uint8_t addr)
{
    return addr >= 1 && addr <= NZONES ? s_zone[addr - 1].id : 0;
}

extern "C" void matter_bridge_print(void)
{
    matter_status_t st;
    matter_bridge_status(&st);
    if (!st.running) {
        printf("Matter is not running\n");
        return;
    }
    printf("QR code     %s\nManual code %s\n", st.qr, st.manual);
    if (st.window_open) {
        printf("Pairing window open, %u s left\n", (unsigned)st.window_left_s);
    } else {
        printf("Pairing window closed ('matter pair' opens it)\n");
    }
    printf("%u controller(s):\n", st.fabric_count);
    for (int i = 0; i < st.fabric_count; i++) {
        printf("  fabric %u  vendor 0x%04X  %s\n", st.fabrics[i].index, st.fabrics[i].vendor, st.fabrics[i].label);
    }
    printf("%u thermostat endpoint(s):\n", st.endpoints);
    for (int i = 0; i < NZONES; i++) {
        if (s_zone[i].id) {
            const mm_thermo_t &m = s_zone[i].last;
            printf("  ep %-3u stat %-2d %-16s %s  local %s%d.%02d  set %d.%02d  mode %s%s%s\n", s_zone[i].id, i + 1,
                   s_zone[i].name, m.reachable ? "reachable  " : "unreachable", m.local_null ? "null " : "",
                   m.local_t / 100, (m.local_t < 0 ? -m.local_t : m.local_t) % 100, m.heat_setpoint / 100,
                   m.heat_setpoint % 100, m.system_mode == MM_SYSTEM_MODE_HEAT ? "Heat" : "Off",
                   m.running_state ? "  heating" : "", m.keypad_lockout ? "  locked" : "");
        }
    }
}
