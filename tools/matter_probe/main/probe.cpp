#include <esp_matter.h>
using namespace esp_matter;
extern "C" void app_main(void)
{
    node::config_t nc;
    node_t *n = node::create(&nc, nullptr, nullptr);
    endpoint::aggregator::config_t ac;
    endpoint::aggregator::create(n, &ac, ENDPOINT_FLAG_NONE, nullptr);
    esp_matter::start(nullptr);
}
