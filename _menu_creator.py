"""Functions that handle creation of the Nuke menu."""

from __future__ import annotations

import logging
import os

import nuke  # ty:ignore[unresolved-import]

try:
    from TVectorBlurCUDA._consts import (
        ICON_FILENAME,
        MENU_NAME,
        NODE_CLASS_NAME,
        PLUGIN_LOADED_ENV_VAR,
        RESOURCE_PATH_ADDED_ENV_VAR,
        RESOURCES_PATH,
        normalized_path,
    )
    from TVectorBlurCUDA._plugin_loader import ensure_node_class_loaded
except Exception:
    from _consts import (
        ICON_FILENAME,
        MENU_NAME,
        NODE_CLASS_NAME,
        PLUGIN_LOADED_ENV_VAR,
        RESOURCE_PATH_ADDED_ENV_VAR,
        RESOURCES_PATH,
        normalized_path,
    )
    from _plugin_loader import ensure_node_class_loaded

logger = logging.getLogger(__name__)


def create_node() -> None:
    try:
        ensure_node_class_loaded()
        nuke.createNode(NODE_CLASS_NAME)
    except Exception as error:
        nuke.tprint(f"[{NODE_CLASS_NAME}] Unable to create node '{NODE_CLASS_NAME}': {error}")


def _create_menu() -> None:
    toolbar = nuke.menu("Nodes")
    menu = toolbar.findItem(MENU_NAME)
    if menu is None:
        menu = toolbar.addMenu(MENU_NAME, ICON_FILENAME)

    command_path = f"{MENU_NAME}/{NODE_CLASS_NAME}"
    if toolbar.findItem(command_path) is None:
        callback = f"import {__name__} as _menu; _menu.create_node()"
        menu.addCommand(NODE_CLASS_NAME, callback, None, ICON_FILENAME)


def add_menu() -> None:
    _add_menu_dependencies_to_plugin_path()
    _create_menu()

    if os.getenv(PLUGIN_LOADED_ENV_VAR) != "1":
        logger.warning("%s menu created, but plugin binary is not loaded yet.", NODE_CLASS_NAME)


def _add_menu_dependencies_to_plugin_path() -> None:
    resources_path = normalized_path(RESOURCES_PATH)
    if os.getenv(RESOURCE_PATH_ADDED_ENV_VAR) == resources_path:
        return

    nuke.pluginAppendPath(resources_path)
    os.environ[RESOURCE_PATH_ADDED_ENV_VAR] = resources_path
