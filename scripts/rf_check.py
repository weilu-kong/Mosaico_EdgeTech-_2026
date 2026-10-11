#!/usr/bin/env python3
"""Static RF-safety scan of the application sources (Phase 2: source scan only).

Fails (exit 1) when any source file references a Wi-Fi, Bluetooth, ESP-NOW,
IEEE 802.15.4, Thread/Zigbee/Matter, mesh, lwIP/netif, esp_phy radio, coexistence
or RF-test API, or when a sdkconfig fragment enables a radio.

THIS IS A SOFTWARE AUDIT. It is not an RF measurement, not a statement about the
SDK/BSP/recovery firmware, and not a compliance conclusion. The product status
remains "RF Verification Pending" until a real measurement and the applicable
Japan radio-law review are complete.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGETS = [ROOT / "app/mosaico_experience/main", ROOT / "app/mosaico_experience/host", ROOT / "firmware"]
SOURCE_EXT = {".c", ".h", ".cpp", ".hpp", ".cc", ".txt", ".cmake", ".yml", ".yaml", ".json", ".py"}
CONFIG_NAMES = {"sdkconfig", "sdkconfig.defaults", "sdkconfig.defaults.voice", "sdkconfig.ci"}

SOURCE_PATTERNS = {
    "wifi": r"\besp_wifi\b|\besp_wifi_|\bwifi_(init|config|mode|event|sta|ap|scan)|\bWIFI_(INIT|EVENT|MODE|IF_)|esp_smartconfig|esp_wps|esp_eap",
    "bluetooth": r"\besp_bt\b|\besp_bt_|\besp_ble|\besp_gap|\besp_gatt|\bnimble|\bble_hs|\bbluedroid|\bhci_|esp_bluedroid|esp_hidd|esp_hidh|bt_controller",
    "esp_now": r"\besp_now\b|\besp_now_",
    "ieee802154": r"\besp_ieee802154|\bieee802154|\bopenthread|\besp_openthread|\bzigbee|esp_zb_|\besp_zigbee",
    "network_stack": r"\besp_netif|\blwip|\besp_http|\besp_https|\bmqtt|\bmdns|\bsockets?\.h|<sys/socket|\bconnect\(|\bgetaddrinfo|\bcurl_|\bhttplib|\burllib|\brequests\.",
    "phy_coex": r"\besp_phy|\bphy_init|\besp_coex|\bcoexist|\bregister_chipv7|\brtc_clk_|esp_wifi_set_ps|esp_wifi_set_max_tx_power|\besp_wifi_set_country",
    "rf_test": r"\besp_phy_rftest|\bcw_test|\brftest|\bcertification|\bwifi_test|esp_phy_cert",
    "matter_net": r"\besp_matter|\besp_rmaker|\bcsa_|\bblufi",
}
CONFIG_PATTERNS = [
    (re.compile(r"^\s*CONFIG_(ESP_WIFI_ENABLED|ESP32_WIFI_ENABLED|ESP_HOST_WIFI_ENABLED)\s*=\s*y", re.M), "Wi-Fi enabled"),
    (re.compile(r"^\s*CONFIG_BT_ENABLED\s*=\s*y", re.M), "Bluetooth enabled"),
    (re.compile(r"^\s*CONFIG_(BT_NIMBLE_ENABLED|BT_BLUEDROID_ENABLED)\s*=\s*y", re.M), "BLE host enabled"),
    (re.compile(r"^\s*CONFIG_(IEEE802154_ENABLED|OPENTHREAD_ENABLED)\s*=\s*y", re.M), "IEEE 802.15.4/Thread enabled"),
    (re.compile(r"^\s*CONFIG_ESP_COEX_ENABLED\s*=\s*y", re.M), "coexistence enabled"),
    (re.compile(r"^\s*CONFIG_ESP_WIFI_NVS_ENABLED\s*=\s*y", re.M), "Wi-Fi NVS enabled"),
    (re.compile(r"^\s*CONFIG_LWIP_[A-Z_]*ENABLE[A-Z_]*\s*=\s*y", re.M), "lwIP feature enabled"),
]
# In a generated `sdkconfig` the radios must be positively absent: either the symbol
# is not set, or it does not exist for the target. A symbol that is simply missing from
# sdkconfig.defaults is NOT evidence (ESP-IDF defaults them on), so the final sdkconfig
# of the firmware build (Phase 3) must be scanned too.
# NOTE (ESP-IDF 6.2-dev 7b9cc1ac, target esp32s31): CONFIG_ESP_WIFI_ENABLED and CONFIG_ESP_COEX_ENABLED are hidden,
# SoC-derived symbols (no prompt). They are =y whatever sdkconfig.defaults says, so they cannot be used as evidence.
# The guard for those is the link-content audit: scripts/rf_map_check.py.
MUST_BE_OFF = ["CONFIG_BT_ENABLED", "CONFIG_IEEE802154_ENABLED", "CONFIG_OPENTHREAD_ENABLED"]
STRIP_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)

# The one reviewed exception: firmware/main/dev_rf_probe.c looks for radio entry points in the running image WITHOUT
# calling them (weak undefined symbols whose address is compared with NULL). Only two exact line shapes are exempt and
# only for these names; any other radio-related text in that file, and any use of these names anywhere else, is a finding.
PROBE_FILE = "dev_rf_probe.c"
PROBE_ALLOWED = {"esp_wifi_init", "esp_wifi_start", "esp_now_init", "esp_bt_controller_init", "esp_bt_controller_enable",
                 "esp_ieee802154_enable", "esp_ieee802154_receive"}
PROBE_DECL = re.compile(r"^extern const char (\w+) __attribute__\(\(weak\)\);$")
PROBE_ROW = re.compile(r'^\s*\{"(\w+)", &(\w+)\},$')


def strip_probe_lines(body: str) -> str:
    out = []
    for line in body.split("\n"):
        d, r = PROBE_DECL.match(line), PROBE_ROW.match(line)
        if d and d.group(1) in PROBE_ALLOWED:
            out.append("")
        elif r and r.group(1) == r.group(2) and r.group(1) in PROBE_ALLOWED:
            out.append("")
        else:
            out.append(line)
    return "\n".join(out)


def scan_file(path: Path) -> list[dict]:
    hits = []
    text = path.read_text(encoding="utf-8", errors="replace")
    if path.name in CONFIG_NAMES or path.name.startswith("sdkconfig"):
        for rx, label in CONFIG_PATTERNS:
            if path.name == "sdkconfig" and label in ("Wi-Fi enabled", "coexistence enabled", "Wi-Fi NVS enabled", "lwIP feature enabled"):
                continue  # SoC-derived in a generated sdkconfig; see rf_map_check.py
            if rx.search(text):
                hits.append({"file": str(path), "kind": "config", "match": label})
        if path.name == "sdkconfig":
            for sym in MUST_BE_OFF:
                if not re.search(rf"^(# {sym} is not set|{sym}=n)", text, re.M):
                    hits.append({"file": str(path), "kind": "config", "match": f"{sym} is not explicitly off"})
        return hits
    body = STRIP_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text) if path.suffix in {".c", ".h", ".cpp", ".hpp", ".cc"} else text
    if path.name == PROBE_FILE:
        body = strip_probe_lines(body)
    for kind, pattern in SOURCE_PATTERNS.items():
        for m in re.finditer(pattern, body):
            line = body.count("\n", 0, m.start()) + 1
            hits.append({"file": str(path), "line": line, "kind": kind, "match": m.group(0)})
    return hits


def main() -> int:
    targets = [Path(a) for a in sys.argv[1:] if not a.startswith("--")] or DEFAULT_TARGETS
    files: list[Path] = []
    for t in targets:
        if t.is_file():
            files.append(t)
        elif t.is_dir():
            files += [p for p in sorted(t.rglob("*")) if p.is_file() and (p.suffix in SOURCE_EXT or p.name in CONFIG_NAMES or p.name.startswith("sdkconfig"))]
    findings = []
    for f in files:
        if (f.name in {"rf_check.py", "exp_font_data.c"} or
                any(part in {"__pycache__", ".git", "managed_components"} or part.startswith("build") for part in f.parts)):
            continue
        findings += scan_file(f)
    report = {
        "scanned_files": len([f for f in files if f.name != "exp_font_data.c"]),
        "findings": findings,
        "result": "FAIL: radio-related references found" if findings else "PASS (static source scan only)",
        "status": "RF Verification Pending",
        "disclaimer": "Software audit only. Not an RF measurement and not a compliance conclusion. Does not cover the ESP-IDF, BSP, "
                      "Recovery/Vibe Mode firmware, ROM code, bootloader or add-on modules.",
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
