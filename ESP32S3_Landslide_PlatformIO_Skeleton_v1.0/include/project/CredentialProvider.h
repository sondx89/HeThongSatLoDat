#pragma once

#if __has_include("project/secrets.h")
#include "project/secrets.h"
#endif

#ifndef PROJECT_WIFI_SSID
#define PROJECT_WIFI_SSID ""
#endif

#ifndef PROJECT_WIFI_PASSWORD
#define PROJECT_WIFI_PASSWORD ""
#endif

#ifndef PROJECT_BLYNK_AUTH_TOKEN
#define PROJECT_BLYNK_AUTH_TOKEN ""
#endif

namespace project::secrets {

inline constexpr char kWifiSsid[] = PROJECT_WIFI_SSID;
inline constexpr char kWifiPassword[] = PROJECT_WIFI_PASSWORD;
inline constexpr char kBlynkAuthToken[] = PROJECT_BLYNK_AUTH_TOKEN;

inline constexpr bool kWifiConfigured =
    kWifiSsid[0] != '\0' && kWifiPassword[0] != '\0';
inline constexpr bool kBlynkConfigured =
    kWifiConfigured && kBlynkAuthToken[0] != '\0';

}  // namespace project::secrets

#undef PROJECT_WIFI_SSID
#undef PROJECT_WIFI_PASSWORD
#undef PROJECT_BLYNK_AUTH_TOKEN
