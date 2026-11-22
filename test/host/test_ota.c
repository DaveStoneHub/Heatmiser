/* Firmware version strings for the update check. */
#include <string.h>

#include "ota_ver.h"
#include "test_util.h"

static void test_parse(void)
{
    ota_ver_t v;
    CHECK(ota_ver_parse("v0.6.1", &v));
    CHECK_EQ(v.major, 0);
    CHECK_EQ(v.minor, 6);
    CHECK_EQ(v.patch, 1);
    CHECK(v.exact);
    CHECK(ota_ver_parse("1.12.30", &v));
    CHECK_EQ(v.minor, 12);
    CHECK_EQ(v.patch, 30);
    CHECK(ota_ver_parse("v0.6.0-3-gabc1234-dirty", &v));
    CHECK(!v.exact);
    CHECK_EQ(v.patch, 0);
    CHECK(!ota_ver_parse("b1aca23", &v));          /* a hash, before the first tag */
    CHECK(!ota_ver_parse("b1aca23-dirty", &v));
    CHECK(!ota_ver_parse("v1.2", &v));
    CHECK(!ota_ver_parse("v1.2.3.4", &v));
    CHECK(!ota_ver_parse("", &v));
    CHECK(!ota_ver_parse(NULL, &v));
    CHECK(!ota_ver_parse("v1..3", &v));
}

static void test_newer(void)
{
    CHECK(ota_ver_newer("v0.6.1", "v0.6.0"));
    CHECK(ota_ver_newer("v0.7.0", "v0.6.9"));
    CHECK(ota_ver_newer("v1.0.0", "v0.99.99"));
    CHECK(ota_ver_newer("v0.6.10", "v0.6.9"));      /* numbers, not text */
    CHECK(!ota_ver_newer("v0.6.0", "v0.6.0"));
    CHECK(!ota_ver_newer("v0.5.9", "v0.6.0"));
    CHECK(!ota_ver_newer("v0.6.0", "v0.6.0-3-gabc1234")); /* built after that release */
    CHECK(ota_ver_newer("v0.6.1", "v0.6.0-3-gabc1234-dirty"));
    CHECK(ota_ver_newer("v0.6.0", "b1aca23"));       /* a build from before releases */
    CHECK(!ota_ver_newer("nightly", "v0.6.0"));      /* not a version tag */
    CHECK(!ota_ver_newer(NULL, "v0.6.0"));
}

static void test_location(void)
{
    char tag[32];
    CHECK(ota_tag_from_location("https://github.com/davestonehub/heatmiser/releases/tag/v0.6.1", tag, sizeof tag));
    CHECK(strcmp(tag, "v0.6.1") == 0);
    CHECK(ota_tag_from_location("https://github.com/o/r/releases/tag/v1.0.0?x=1", tag, sizeof tag));
    CHECK(strcmp(tag, "v1.0.0") == 0);
    /* No release yet: GitHub sends the releases page instead. */
    CHECK(!ota_tag_from_location("https://github.com/o/r/releases", tag, sizeof tag));
    CHECK(!ota_tag_from_location("https://github.com/o/r/releases/tag/", tag, sizeof tag));
    CHECK(!ota_tag_from_location("", tag, sizeof tag));
    CHECK(!ota_tag_from_location(NULL, tag, sizeof tag));
    char small[4];
    CHECK(!ota_tag_from_location("https://github.com/o/r/releases/tag/v0.6.1", small, sizeof small));
}

void test_ota_all(void)
{
    test_parse();
    test_newer();
    test_location();
}
