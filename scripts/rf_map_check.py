#!/usr/bin/env python3
"""Link-content RF audit of a built firmware (ESP-IDF linker map + generated sdkconfig).

Why this exists: on ESP-IDF 6.2-dev for esp32s31, CONFIG_ESP_WIFI_ENABLED and CONFIG_ESP_COEX_ENABLED are hidden
symbols derived from the SoC; they are 'y' regardless of sdkconfig.defaults. A sdkconfig line therefore proves
nothing for them. What can be checked is which radio code is really linked into the image.

THIS IS A SOFTWARE AUDIT OF ONE BUILD. It is not an RF measurement and not a compliance conclusion: the PHY driver
may still be linked (see 'phy_linked'), ROM code is not covered, and nothing here observes the device's emissions.
Usage: scripts/rf_map_check.py <build/project.map> [<sdkconfig>]
"""
from __future__ import annotations

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

# Any member of these archives in the image means a radio stack (or its network stack) is linked: FAIL.
FORBIDDEN = ["libnet80211.a", "libpp.a", "libcoexist.a", "libesp_coex.a", "libmesh.a", "libespnow.a", "libbt.a",
             "libbtbb.a", "libnimble.a", "libble_app.a", "libble_mesh.a", "libopenthread.a", "libieee802154.a",
             "libesp_hal_ieee802154.a", "liblwip.a", "libwapi.a", "libsmartconfig.a", "libmbedtls_wifi.a"]
# Archives that may legitimately contribute only error-code string tables.
ERRCODE_ONLY = ["libesp_wifi.a", "libwpa_supplicant.a", "libesp_netif.a", "libesp_event.a"]
# Reported, not failed: the PHY (RF front-end calibration) driver that IDF core links for sleep handling.
PHY_ARCHIVES = ["libphy.a", "libesp_phy.a"]
SDKCONFIG_MUST_BE_OFF = ["CONFIG_BT_ENABLED", "CONFIG_IEEE802154_ENABLED", "CONFIG_OPENTHREAD_ENABLED"]
SDKCONFIG_SOC_DERIVED = ["CONFIG_ESP_WIFI_ENABLED", "CONFIG_ESP_COEX_ENABLED"]


def included_members(map_text: str) -> dict[str, list[str]]:
    a = map_text.index("Archive member included")
    b = map_text.index("Discarded input sections")
    out: dict[str, list[str]] = defaultdict(list)
    for m in re.finditer(r"^\S*?([^/\s]+\.a)\((\S+?)\)", map_text[a:b], re.M):
        out[m.group(1)].append(m.group(2))
    return out


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mp = Path(sys.argv[1])
    members = included_members(mp.read_text(errors="replace"))
    findings = []
    for arch in FORBIDDEN:
        if members.get(arch):
            findings.append({"kind": "forbidden_archive_linked", "archive": arch, "members": members[arch][:10]})
    for arch in ERRCODE_ONLY:
        extra = [m for m in members.get(arch, []) if "esp_err_codes" not in m]
        if extra:
            findings.append({"kind": "radio_or_network_code_linked", "archive": arch, "members": extra[:10]})
    sdk = {}
    if len(sys.argv) > 2:
        text = Path(sys.argv[2]).read_text(errors="replace")
        for sym in SDKCONFIG_MUST_BE_OFF:
            off = re.search(rf"^(# {sym} is not set|{sym}=n)", text, re.M) is not None
            sdk[sym] = "off" if off else "NOT OFF"
            if not off:
                findings.append({"kind": "sdkconfig", "symbol": sym})
        for sym in SDKCONFIG_SOC_DERIVED:
            sdk[sym] = "y (SoC-derived, cannot be disabled by config; not evidence either way)" if re.search(rf"^{sym}=y", text, re.M) else "n"
    phy = {a: len(members.get(a, [])) for a in PHY_ARCHIVES}
    report = {
        "scope": "link-content audit of one build; not an RF measurement",
        "map": str(mp),
        "sdkconfig": sdk,
        "phy_linked": {"members": phy, "meaning": "PHY (RF front-end) driver code is in the image; nothing here shows it is "
                       "ever executed. Must be settled by runtime check + RF measurement.",
                       "linked": any(phy.values())},
        "findings": findings,
        "result": "FAIL" if findings else "PASS (no Wi-Fi MAC / BT / 802.15.4 / coex / lwIP code linked)",
        "status": "RF Verification Pending",
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
