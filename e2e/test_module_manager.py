#!/usr/bin/env python3
"""Exercise the standalone module manager and classification of tool panels."""
import json
from pathlib import Path
import time
import traceback
import zlib

from e2e_common import SDRPPTestContext, get_lsb_config, stats
from test_desktop_workspace import click, request, screenshot, wait_for, widget


def test_module_manager():
    main, radio = get_lsb_config()
    main.update({"windowSize": {"w": 1280, "h": 720}, "showMenu": True,
                 "theme": "Dark", "transcieverLayout": 0})
    main["moduleInstances"].update({"Bookmarks": {"module": "frequency_manager", "enabled": True},
                                    "Recorder": {"module": "recorder", "enabled": True}})
    main["menuElements"] = [{"name": name, "open": False}
                            for name in ["Module Manager", "Radio", "Bookmarks", "Recorder"]]
    with SDRPPTestContext() as ctx:
        ctx.write_configs(main, radio)
        assert ctx.start(), "Application failed to start"
        click(ctx, "Control Panel", "Modules##workspace_section")
        widget(ctx, "Panel Contents", "Bookmarks##sdrpp_main_menu")

        widget(ctx, "Panel Contents", "Recorder##sdrpp_main_menu")
        click(ctx, "Control Panel", "Settings##workspace_section")
        widget(ctx, "Panel Contents", "Theme##sdrpp_main_menu")

        def settings_are_separate():
            elements = request(ctx, "/layout")["elements"]
            panel = next(item for item in elements if item["type"] == "window" and "/Panel Contents_" in item["name"])
            excluded = {zlib.crc32((name + "##sdrpp_main_menu").encode(), panel["id"])
                        for name in ["Bookmarks", "Recorder", "Module Manager"]}
            header = next(item for item in elements if item["type"] == "window" and "/Radio Header_" in item["name"])
            excluded.add(zlib.crc32(b"SDR++ BROWN##workspace_about", header["id"]))
            return not any(item["type"] == "widget" and item["id"] in excluded for item in elements)
        wait_for(settings_are_separate, "Tool panels, module management, or About remain in the old locations")

        # The manager remains accessible when the entire sidebar is hidden.
        click(ctx, "Radio Header", "Panels##workspace_panels")
        click(ctx, "Radio Header", "Module manager##workspace_modules")
        widget(ctx, "Module manager", "##module_manager_search")
        screenshot(ctx, "module-manager", 1280, 720)
        close = widget(ctx, "Module manager", "Close##module_manager_close")
        assert close["y"] + close["h"] < 720, "Manager footer is clipped"

        click(ctx, "Module manager", "##module_manager_search")
        request(ctx, "/type?text=Bookmarks")
        toggle = "##module_manager_enabled_Bookmarks"
        widget(ctx, "Module manager", toggle, scope="Module instances")
        click(ctx, "Module manager", toggle, scope="Module instances")
        saved = lambda: json.loads((Path(ctx.temp_dir) / "config.json").read_text())
        wait_for(lambda: not saved()["moduleInstances"]["Bookmarks"]["enabled"], "Disabled state was not saved")
        click(ctx, "Module manager", toggle, scope="Module instances")
        wait_for(lambda: saved()["moduleInstances"]["Bookmarks"]["enabled"], "Enabled state was not saved")
        click(ctx, "Module manager", "##module_manager_search")
        for _ in "Bookmarks":
            request(ctx, "/key?key=259")
            time.sleep(0.03)

        # Create an instance of the first available type through the form, then remove it.
        click(ctx, "Module manager", "##module_mod_name", scope="New module instance")
        request(ctx, "/type?text=UI%20test")
        time.sleep(0.1)
        click(ctx, "Module manager", "Add instance##module_mgr_add_btn", scope="New module instance")
        wait_for(lambda: "UI test" in saved()["moduleInstances"], "Add instance did not create and save the module")
        remove = "Remove##module_manager_remove_UI test"
        def confirm(label):
            popup = wait_for(lambda: next((item for item in request(ctx, "/layout")["elements"]
                                          if item["type"] == "popup"), None), "Removal confirmation did not open")
            click(ctx, popup["name"], label + "##module_mgr_confirm_")
        click(ctx, "Module manager", remove, scope="Module instances")
        confirm("No")
        assert "UI test" in saved()["moduleInstances"], "Cancelling removal deleted the module"
        click(ctx, "Module manager", remove, scope="Module instances")
        confirm("Yes")
        wait_for(lambda: "UI test" not in saved()["moduleInstances"], "Remove did not delete and save the instance")
        click(ctx, "Module manager", "Close##module_manager_close")
        click(ctx, "Radio Header", "Module manager##workspace_modules")
        widget(ctx, "Module manager", "##module_manager_search")
        request(ctx, "/key?key=256")  # Escape closes the focused manager.
        time.sleep(0.15)
        click(ctx, "Radio Header", "Panels##workspace_panels")
        click(ctx, "Control Panel", "Modules##workspace_section")
        widget(ctx, "Panel Contents", "Bookmarks##sdrpp_main_menu")

        # Removing the selected receiver must not leave the workspace using a deleted VFO.
        click(ctx, "Radio Header", "Module manager##workspace_modules")
        click(ctx, "Module manager", "Remove##module_manager_remove_Radio", scope="Module instances")
        confirm("Yes")
        wait_for(lambda: "Radio" not in saved()["moduleInstances"], "Selected receiver was not removed")
        click(ctx, "Module manager", "Close##module_manager_close")
        assert request(ctx, "/status")["ready"], "Workspace failed after deleting its selected receiver"


if __name__ == "__main__":
    name = "test_module_manager"
    stats.test_start(name)
    try:
        test_module_manager()
        stats.test_pass(name)
        stats.final_summary(1, 1, 0)
    except Exception as error:
        traceback.print_exc()
        stats.test_fail(name, str(error))
        stats.final_summary(1, 0, 1)
        raise SystemExit(1)
