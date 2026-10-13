#!/usr/bin/env python3
"""Read live ESP-Iris logs and status over USB CDC0 on macOS without requiring aiohttp/Gateway."""
from __future__ import annotations

import argparse
import glob
import struct
import sys
import time
import serial

TOOLS_DIR = "/Users/kongweilu/Development/Mosaico EdgeTech 2026/mosaico-experience/workspace/esp-mosaico-vibe/submodule/esp-mosaico-utils/ESP-Iris/components/esp_iris/tools"
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from iris_gateway.protocol import (
    Capability,
    Channel,
    ControlType,
    EventType,
    Frame,
    FrameDecoder,
    TlvTag,
    decode_tlv,
    encode_frame,
    tlv_u32,
    tlv_u64,
)

LOG_CREDIT_GRANT = 256 * 1024
LOG_CREDIT_LOW_WATER = 128 * 1024


def find_iris_port(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    for p in sorted(glob.glob("/dev/cu.usbmodem*")):
        if "11201" in p:
            continue
        return p
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Read live logs from ESP-Iris over USB CDC0")
    ap.add_argument("-p", "--port", default=None, help="Serial port (default: auto-detect /dev/cu.usbmodem*, skipping 11201)")
    ap.add_argument("-d", "--duration", type=float, default=20.0, help="Seconds to stream logs (0 = forever)")
    args = ap.parse_args()

    print("Waiting for ESP-Iris USB CDC0 port...")
    port_name = None
    t_wait = time.monotonic()
    while time.monotonic() - t_wait < 30.0:
        port_name = find_iris_port(args.port)
        if port_name:
            break
        time.sleep(0.5)

    if not port_name:
        print("ERROR: No ESP-Iris USB port found (only 11201 or none). Make sure firmware with ESP_IRIS_TRANSPORT_USB=y is running.")
        return 1

    print(f"Opening ESP-Iris USB CDC0 on {port_name}...")
    ser = serial.Serial(port=port_name, baudrate=115200, timeout=0.1, write_timeout=2.0)
    ser.dtr = True
    ser.rts = False

    decoder = FrameDecoder()
    seq = [0] * 16
    session_id = 0
    reopen_requested = False
    reopen_session_id: int | None = None
    log_credit = 0
    ready = False

    def send_frame(ch: Channel, ftype: int, payload: bytes = b"", flags: int = 0, req_id: int = 0) -> None:
        seq[int(ch)] = (seq[int(ch)] + 1) & 0xFFFFFFFF
        raw = encode_frame(
            Frame(
                channel=int(ch),
                type=int(ftype),
                flags=flags,
                session_id=session_id,
                request_id=req_id,
                sequence=seq[int(ch)],
                payload=payload,
            )
        )
        ser.write(raw)
        ser.flush()

    t_start = time.monotonic()
    try:
        while args.duration <= 0 or (time.monotonic() - t_start) < args.duration:
            chunk = ser.read(4096)
            if not chunk:
                continue
            for frame in decoder.feed(chunk):
                if frame.channel == Channel.CONTROL and frame.type == ControlType.HELLO:
                    fields = decode_tlv(frame.payload)
                    dev_id = fields.get(int(TlvTag.DEVICE_ID), b"").hex()
                    proj = fields.get(int(TlvTag.PROJECT_NAME), b"").decode("utf-8", errors="replace")
                    ver = fields.get(int(TlvTag.APP_VERSION), b"").decode("utf-8", errors="replace")
                    caps = tlv_u64(fields, TlvTag.CAPABILITIES)
                    session_id = frame.session_id
                    if reopen_session_id is not None and session_id != reopen_session_id:
                        reopen_session_id = None
                        seq = [0] * 16
                    if (caps & int(Capability.SESSION_REOPEN)) and not reopen_requested:
                        reopen_requested = True
                        reopen_session_id = session_id
                    ack_flags = (1 << 5) if reopen_session_id is not None else 0
                    print(f"[IRIS HELLO] device_id={dev_id} project={proj} version={ver} session={session_id} reopen={bool(ack_flags)}")
                    send_frame(Channel.CONTROL, ControlType.HELLO_ACK, flags=ack_flags)
                    if not ack_flags and not ready:
                        credit_payload = bytes([int(Channel.LOG), 0, 0, 0]) + struct.pack("<I", LOG_CREDIT_GRANT)
                        send_frame(Channel.CONTROL, ControlType.CREDIT, credit_payload)
                        log_credit += LOG_CREDIT_GRANT
                        send_frame(Channel.CONTROL, ControlType.STATUS_REQUEST, req_id=1)
                        ready = True
                elif frame.channel == Channel.CONTROL and frame.type == ControlType.STATUS_RESPONSE:
                    fields = decode_tlv(frame.payload)
                    up_ms = tlv_u64(fields, TlvTag.UPTIME_US) // 1000
                    free_int = tlv_u32(fields, TlvTag.FREE_INTERNAL)
                    free_psram = tlv_u32(fields, TlvTag.FREE_SPIRAM)
                    dropped = tlv_u32(fields, TlvTag.LOG_DROPPED)
                    print(f"[IRIS STATUS] uptime={up_ms}ms free_internal={free_int}B free_spiram={free_psram}B log_dropped={dropped}B")
                elif frame.channel == Channel.LOG:
                    if len(frame.payload) >= 16:
                        mono_us, dropped, src, flags, length = struct.unpack_from("<QIBBH", frame.payload)
                        text = frame.payload[16:16 + length].decode("utf-8", errors="replace")
                        log_credit = max(0, log_credit - len(frame.payload))
                        sys.stdout.write(text)
                        sys.stdout.flush()
                        if log_credit < LOG_CREDIT_LOW_WATER:
                            credit_payload = bytes([int(Channel.LOG), 0, 0, 0]) + struct.pack("<I", LOG_CREDIT_GRANT)
                            send_frame(Channel.CONTROL, ControlType.CREDIT, credit_payload)
                            log_credit += LOG_CREDIT_GRANT
                elif frame.channel == Channel.EVENT:
                    fields = decode_tlv(frame.payload)
                    ename = EventType(frame.type).name if frame.type in EventType._value2member_map_ else f"0x{frame.type:02x}"
                    up_ms = tlv_u64(fields, TlvTag.UPTIME_US) // 1000
                    print(f"\n[IRIS EVENT] {ename} uptime={up_ms}ms reset_reason={tlv_u32(fields, TlvTag.RESET_REASON)}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
