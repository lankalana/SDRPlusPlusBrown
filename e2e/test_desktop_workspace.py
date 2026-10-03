#!/usr/bin/env python3
"""Exercise the desktop UI through the rendered widgets and debug HTTP API."""
import json
import math
import os
from pathlib import Path
import random
import struct
import time
import traceback
import urllib.parse
import urllib.request
import wave
import zlib

from e2e_common import SDRPPTestContext, get_lsb_config, stats


def request(ctx, path):
    with urllib.request.urlopen(ctx.base_url + path, timeout=5) as response:
        return json.load(response)


def wait_for(predicate, message, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.03)
    raise AssertionError(message)


def widget(ctx, window_title, label):
    def locate():
        elements = request(ctx, "/layout")["elements"]
        windows = [item for item in elements if item["type"] == "window"
                   and (f"/{window_title}_" in item["name"] or item["name"] == window_title)]
        for window in windows:
            widget_id = zlib.crc32(label.encode(), window["id"])
            for item in elements:
                if item["type"] == "widget" and item["id"] == widget_id:
                    return item
        return None
    return wait_for(locate, f"Cannot find {label} in {window_title}")


def click(ctx, window_title, label):
    item = widget(ctx, window_title, label)
    request(ctx, f'/click?x={item["x"] + item["w"] / 2}&y={item["y"] + item["h"] / 2}')
    time.sleep(0.15)


def screenshot(ctx, name, width, height):
    artifact_dir = os.environ.get("E2E_UI_ARTIFACT_DIR")
    path = "/screenshot"
    # Request a fresh frame before reading the cached capture.
    with urllib.request.urlopen(ctx.base_url + path, timeout=5) as response:
        response.read()
    time.sleep(0.1)
    deadline = time.monotonic() + 4
    while time.monotonic() < deadline:
        with urllib.request.urlopen(ctx.base_url + path, timeout=5) as response:
            bitmap = response.read()
        if bitmap[:2] == b"BM":
            actual_width, actual_height = struct.unpack_from("<ii", bitmap, 18)
            assert actual_width > 0 and actual_height > 0
            # Framebuffer dimensions can differ from logical dimensions on HiDPI displays.
            assert abs(actual_width / actual_height - width / height) < 0.1
            pixels = bitmap[54:]
            assert len(set(pixels)) > 30, "Framebuffer is blank"
            if artifact_dir:
                destination = Path(artifact_dir)
                destination.mkdir(parents=True, exist_ok=True)
                (destination / f"{name}.bmp").write_bytes(bitmap)
            return
        time.sleep(0.05)
    raise AssertionError("No rendered screenshot returned")


def test_workspace():
    main, radio = get_lsb_config()
    main.update({"windowSize": {"w": 1280, "h": 720}, "showMenu": True,
                 "theme": "Dark", "transcieverLayout": 0, "min": -120.0, "max": 0.0})
    with SDRPPTestContext() as ctx:
        ctx.write_configs(main, radio)
        assert ctx.start(), "Application failed to start"
        wait_for(lambda: any("/Spectrum Workspace_" in element["name"]
                             for element in request(ctx, "/layout")["elements"]),
                 "Spectrum workspace was not rendered")
        frequency = widget(ctx, "Radio Header", "##frequency_display")
        assert frequency["w"] > 200 and frequency["h"] > 20
        screenshot(ctx, "desktop-workspace", 1280, 720)

        # A single click changes a slider's absolute level; it does not need a drag.
        def levels():
            return json.loads((Path(ctx.temp_dir) / "config.json").read_text())
        floor_before = levels()["min"]
        click(ctx, "Spectrum Workspace", "Floor##workspace_floor")
        wait_for(lambda: levels()["min"] != floor_before, "Floor slider did not change its level")
        ceiling_before = levels()["max"]
        click(ctx, "Spectrum Workspace", "Ceiling##workspace_ceiling")
        wait_for(lambda: levels()["max"] != ceiling_before, "Ceiling slider did not change its level")
        assert -200 <= levels()["min"] <= levels()["max"] - 10
        assert levels()["max"] <= 0

        click(ctx, "Receiver Controls", "USB##workspace_mode")
        wait_for(lambda: ctx.module_cmd("Radio", "get_demod").get("id") == 4,
                 "USB button did not change the receiver mode")
        click(ctx, "Receiver Controls", "LSB##workspace_mode")
        wait_for(lambda: ctx.module_cmd("Radio", "get_demod").get("id") == 6,
                 "LSB button did not restore the receiver mode")

        # The final digit's upper half increments Hz even inside the header child window.
        request(ctx, f'/click?x={frequency["x"] + frequency["w"] - 6}&y={frequency["y"] + 5}')
        def tuned_frequency():
            saved = json.loads((Path(ctx.temp_dir) / "config.json").read_text())
            return saved["frequency"] + saved.get("vfoOffsets", {}).get("Radio", 0)
        wait_for(lambda: tuned_frequency() == 7100001, "Frequency digit hitbox did not tune by 1 Hz")

        click(ctx, "Control Panel", "Settings##workspace_section")
        click(ctx, "Control Panel", "##workspace_search")
        request(ctx, "/type?text=Theme")
        time.sleep(0.15)
        widget(ctx, "Panel Contents", "Theme##sdrpp_main_menu")
        request(ctx, "/key?key=259")  # Backspace: clear the five-character search.
        for _ in range(4):
            time.sleep(0.05)
            request(ctx, "/key?key=259")
        click(ctx, "Control Panel", "Radio##workspace_section")
        spectrum_before = next(element for element in request(ctx, "/layout")["elements"]
                               if element["type"] == "window" and "/Spectrum Workspace_" in element["name"])
        click(ctx, "Radio Header", "Panels##workspace_panels")
        wait_for(lambda: any(element["type"] == "window" and "/Spectrum Workspace_" in element["name"]
                             and element["w"] > spectrum_before["w"] + 150
                             for element in request(ctx, "/layout")["elements"]),
                 "Hiding the panels did not expand the spectrum")
        click(ctx, "Radio Header", "Panels##workspace_panels")


def test_compact_and_scaled():
    for width, height, scale, name in [(800, 600, 1.0, "desktop-compact"),
                                       (1600, 1000, 1.5, "desktop-scaled")]:
        main, radio = get_lsb_config()
        main.update({"windowSize": {"w": width, "h": height}, "uiScale": scale,
                     "showMenu": True, "theme": "Dark", "transcieverLayout": 0,
                     "showAudioWaterfall": True})
        with SDRPPTestContext() as ctx:
            ctx.write_configs(main, radio)
            assert ctx.start(), "Application failed to start"
            wait_for(lambda: any("/Spectrum Workspace_" in element["name"]
                                 for element in request(ctx, "/layout")["elements"]),
                     "Spectrum workspace was not rendered")
            screenshot(ctx, name, width, height)
            assert widget(ctx, "Radio Header", "##frequency_display")["x"] >= 0
            assert widget(ctx, "Receiver Controls", "LSB##workspace_mode")["w"] > 0
            assert any("/Output Dock_" in item["name"] for item in request(ctx, "/layout")["elements"])
            if scale == 1.0:
                click(ctx, "Spectrum Workspace", "Levels##workspace_levels")
                wait_for(lambda: any(item["type"] == "window" and item["name"].startswith("##Popup_")
                                     for item in request(ctx, "/layout")["elements"]),
                         "Compact layout cannot access display levels")


def test_live_reception():
    main, radio = get_lsb_config()
    main.update({"windowSize": {"w": 1280, "h": 800}, "showMenu": False,
                 "theme": "Dark", "transcieverLayout": 0, "source": "File",
                 "min": -100.0, "max": -10.0, "fftSize": 16384})
    main["moduleInstances"]["File Source"] = {"module": "file_source", "enabled": True}
    main["streams"]["Radio"]["sink"] = "NullAudioSink"
    with SDRPPTestContext() as ctx:
        iq_path = Path(ctx.temp_dir) / "workspace_7100000Hz.wav"
        rng = random.Random(17)
        samples = bytearray()
        sample_rate = 96000
        tones = [(-18000, 0.08), (-1500, 0.2), (12000, 0.05)]
        for index in range(sample_rate):
            real, imag = rng.gauss(0, 0.0003), rng.gauss(0, 0.0003)
            for frequency, amplitude in tones:
                phase = 2 * math.pi * frequency * index / sample_rate
                real += amplitude * math.cos(phase)
                imag += amplitude * math.sin(phase)
            samples.extend(struct.pack("<hh", int(real * 32767), int(imag * 32767)))
        with wave.open(str(iq_path), "wb") as recording:
            recording.setparams((2, 2, sample_rate, 0, "NONE", "not compressed"))
            recording.writeframes(samples)
        ctx.write_configs(main, radio)
        (Path(ctx.temp_dir) / "file_source_config.json").write_text(
            json.dumps({"path": str(iq_path)}))
        assert ctx.start(), "Application failed to start"
        assert ctx.module_cmd("File Source", "set_path", str(iq_path)).get("status") == "ok"
        click(ctx, "Radio Header", "Start RX##workspace_play")
        wait_for(lambda: request(ctx, "/sdr/status")["playing"], "Start RX did not start reception")
        time.sleep(1)
        screenshot(ctx, "desktop-live", 1280, 800)

        click(ctx, "Receiver Controls", "USB##workspace_mode")
        wait_for(lambda: ctx.module_cmd("Radio", "get_demod").get("id") == 4,
                 "Cannot change mode during reception")
        click(ctx, "Radio Header", "Stop RX##workspace_play")
        wait_for(lambda: not request(ctx, "/sdr/status")["playing"], "Stop RX did not stop reception")


if __name__ == "__main__":
    passed = 0
    cases = [test_workspace, test_compact_and_scaled, test_live_reception]
    for case in cases:
        stats.test_start(case.__name__)
        try:
            case()
            stats.test_pass(case.__name__)
            passed += 1
        except Exception as error:
            traceback.print_exc()
            stats.test_fail(case.__name__, str(error))
    stats.final_summary(len(cases), passed, len(cases) - passed)
    raise SystemExit(0 if passed == len(cases) else 1)
