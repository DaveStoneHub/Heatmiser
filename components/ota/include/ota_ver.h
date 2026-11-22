/*
 * Firmware version strings, for the update check. Pure C (host tested).
 *
 * Releases are tagged vMAJOR.MINOR.PATCH. A build from git between releases
 * has a version like "v0.6.0-3-gabc1234-dirty" (git describe), or just a
 * commit hash before the first tag.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int major, minor, patch;
    bool exact;          /* nothing after the three numbers: a release build */
} ota_ver_t;

/* Parse "v1.2.3" or "1.2.3", with anything after it. False if s does not
 * start with a version. */
bool ota_ver_parse(const char *s, ota_ver_t *out);

/* Whether the release tagged `latest` should be offered to a bridge running
 * `running`: it is a version, and either running is not one (a build from
 * before the first release) or latest is higher. A build made after a
 * release (v0.6.0-3-g...) is not offered that same release again. */
bool ota_ver_newer(const char *latest, const char *running);

/* The tag from a GitHub "releases/latest" redirect, e.g.
 * "https://github.com/o/r/releases/tag/v0.6.1" gives "v0.6.1". False if the
 * location is not a release tag (the repository has no release yet). */
bool ota_tag_from_location(const char *location, char *tag, size_t cap);

#ifdef __cplusplus
}
#endif
