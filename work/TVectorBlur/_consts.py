"""Shared product constants for the TVectorBlur Nuke plugin."""

from __future__ import annotations

import os
from pathlib import Path

PACKAGE_PATH = Path(__file__).resolve().parent
INSTALLATION_PATH = str(PACKAGE_PATH)
RESOURCES_PATH = str(PACKAGE_PATH / "resources")

PRODUCT_NAME = "TVectorBlur"
PRODUCT_VERSION = "1.0.0"
PRODUCT_RELEASE_YEAR = "2026"
PRODUCT_VENDOR = "Thomas Petroni"
PRODUCT_VENDOR_URL = "https://www.linkedin.com/in/thomas-petroni/"

NODE_CLASS_NAME = PRODUCT_NAME
MENU_NAME = PRODUCT_NAME
PLUGIN_BIN_DIRECTORY = "bin"
ICON_FILENAME = "VectorBlur.png"

PLUGIN_LOADED_ENV_VAR = "TVECTORBLUR_LOADED"
PLUGIN_BINARY_PATH_ENV_VAR = "TVECTORBLUR_PLUGIN_BIN_PATH"
RESOURCE_PATH_ADDED_ENV_VAR = "TVECTORBLUR_RESOURCE_PATH"
HOOKS_SETUP_ENV_VAR = "TVECTORBLUR_HOOKS_DONE"


def normalized_path(path: str) -> str:
    return path.replace(os.sep, "/")


def product_credits_html() -> str:
    return (
        f"{PRODUCT_NAME} {PRODUCT_VERSION} - {PRODUCT_RELEASE_YEAR} - "
        f"<a href='{PRODUCT_VENDOR_URL}' "
        "style='text-decoration: underline; color: #9ec3ff;'>"
        f"{PRODUCT_VENDOR}"
        "</a>"
    )
