#include "ota_ver.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool num(const char **p, int *out)
{
    if (!isdigit((unsigned char)**p)) {
        return false;
    }
    long v = 0;
    while (isdigit((unsigned char)**p)) {
        v = v * 10 + (**p - '0');
        if (v > 99999) {
            return false;
        }
        (*p)++;
    }
    *out = (int)v;
    return true;
}

bool ota_ver_parse(const char *s, ota_ver_t *out)
{
    if (!s) {
        return false;
    }
    const char *p = s;
    if (*p == 'v' || *p == 'V') {
        p++;
    }
    ota_ver_t v = { 0 };
    if (!num(&p, &v.major) || *p++ != '.' || !num(&p, &v.minor) || *p++ != '.' || !num(&p, &v.patch)) {
        return false;
    }
    if (isdigit((unsigned char)*p) || *p == '.') {
        return false; /* 1.2.3.4 */
    }
    v.exact = *p == '\0';
    *out = v;
    return true;
}

static int cmp(const ota_ver_t *a, const ota_ver_t *b)
{
    if (a->major != b->major) {
        return a->major < b->major ? -1 : 1;
    }
    if (a->minor != b->minor) {
        return a->minor < b->minor ? -1 : 1;
    }
    if (a->patch != b->patch) {
        return a->patch < b->patch ? -1 : 1;
    }
    return 0;
}

bool ota_ver_newer(const char *latest, const char *running)
{
    ota_ver_t l, r;
    if (!ota_ver_parse(latest, &l)) {
        return false;
    }
    if (!ota_ver_parse(running, &r)) {
        return true;
    }
    return cmp(&l, &r) > 0;
}

bool ota_tag_from_location(const char *location, char *tag, size_t cap)
{
    static const char key[] = "/releases/tag/";
    const char *p = location ? strstr(location, key) : NULL;
    if (!p || cap == 0) {
        return false;
    }
    p += sizeof key - 1;
    size_t n = strcspn(p, "/?#");
    if (n == 0 || n >= cap) {
        return false;
    }
    memcpy(tag, p, n);
    tag[n] = '\0';
    return true;
}
