"""Shared product constants for the TVectorBlur Nuke plugin."""

from __future__ import annotations

import os
from pathlib import Path

PACKAGE_PATH = Path(__file__).resolve().parent
INSTALLATION_PATH = str(PACKAGE_PATH)
RESOURCES_PATH = str(PACKAGE_PATH / "resources")

PRODUCT_NAME = "TVectorBlur"
PRODUCT_VERSION = "2.0.1"
PRODUCT_RELEASE_YEAR = "2026"
PRODUCT_VENDOR = "Thomas Petroni"
PRODUCT_VENDOR_URL = "https://www.linkedin.com/in/thomas-petroni/"

NODE_CLASS_NAME = PRODUCT_NAME
MENU_NAME = PRODUCT_NAME
PLUGIN_BIN_DIRECTORY = "bin"
ICON_FILENAME = "TVectorBlur.png"

PLUGIN_LOADED_ENV_VAR = "TVECTORBLUR_LOADED"
PLUGIN_BINARY_PATH_ENV_VAR = "TVECTORBLUR_PLUGIN_BIN_PATH"
RESOURCE_PATH_ADDED_ENV_VAR = "TVECTORBLUR_RESOURCE_PATH"
ICON_SANITIZER_SETUP_ENV_VAR = "TVECTORBLUR_ICON_SANITIZER_DONE"


def normalized_path(path: str) -> str:
    """Normalize a filesystem path for Nuke plugin registration."""
    return path.replace(os.sep, "/")


def product_credits_html() -> str:
    """Return the shared rich-text credits label shown in the node UI."""
    return (
        f"{PRODUCT_NAME} {PRODUCT_VERSION} - {PRODUCT_RELEASE_YEAR} - "
        f"<a href='{PRODUCT_VENDOR_URL}' "
        "style='text-decoration: underline; color: #9ec3ff;'>"
        f"{PRODUCT_VENDOR}"
        "</a>"
    )
