#!/usr/bin/env python3
"""Offline checks for the developer packaging files, not Flathub approval.

Run appstreamcli and desktop-file-validate separately for standards validation.
"""
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


def main():
    packaging = Path(__file__).resolve().parent
    manifest_path = packaging / "io.github.murmur_voice.Murmur.json"
    manifest = json.loads(manifest_path.read_text())
    app_id = manifest["app-id"]
    assert re.fullmatch(r"[a-z][a-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*){2,4}", app_id), "Invalid application ID"
    metadata_path = packaging / f"{app_id}.metainfo.xml"
    component = ET.parse(metadata_path).getroot()
    assert component.attrib["type"] == "desktop-application"
    assert component.findtext("id") == app_id, "Manifest and metadata IDs differ"
    assert component.findtext("project_license") == "MIT", "Metadata license disagrees with LICENSE"
    assert component.findtext("metadata_license") == "CC0-1.0"
    assert component.findtext("launchable") == f"{app_id}.desktop", "Exported desktop file mismatch"
    assert component.find("developer/name") is not None
    desktop = (packaging / manifest["rename-desktop-file"]).read_text()
    assert "Exec=murmur\n" in desktop and "Icon=murmur\n" in desktop
    assert (packaging / f'{manifest["rename-icon"]}.svg').is_file()
    assert manifest["command"] == "murmur"
    allowed = {
        "--share=ipc", "--socket=wayland", "--socket=fallback-x11",
        "--device=dri", "--filesystem=xdg-run/pipewire-0:ro",
        "--env=MURMUR_NO_SYSTEMD=1",
    }
    assert set(manifest["finish-args"]) == allowed, "Review changed sandbox permissions"
    module = next(item for item in manifest["modules"] if item["name"] == "murmur")
    assert module["buildsystem"] == "cmake-ninja"
    assert "-DMURMUR_FLATPAK=ON" in module["config-opts"]
    assert module["run-tests"] is True
    source = module["sources"][0]
    if source["type"] == "dir":
        assert (packaging / source["path"] / "CMakeLists.txt").is_file(), "Local source path is invalid"
    else:
        assert source["type"] == "archive"
        assert source["url"].startswith("https://")
        assert re.fullmatch(r"[a-f0-9]{64}", source["sha256"])
    print("Developer manifest, metadata identity, desktop/icon and permission checks passed.")
    print("These offline checks do not establish a successful sandbox build or publication approval.")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, ValueError, KeyError, ET.ParseError) as error:
        print(f"Packaging check failed: {error}", file=sys.stderr)
        sys.exit(1)
