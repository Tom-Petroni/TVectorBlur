"""Functions that handle creation of the Nuke menu."""

import logging
import os

import nuke  # ty:ignore[unresolved-import]

try:
    from TVectorBlur._consts import (
        ICON_FILENAME,
        MENU_NAME,
        NODE_CLASS_NAME,
        PLUGIN_LOADED_ENV_VAR,
        RESOURCE_PATH_ADDED_ENV_VAR,
        RESOURCES_PATH,
        normalized_path,
    )
    from TVectorBlur._plugin_loader import ensure_node_class_loaded
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


def create_node():
    """Create node and force-load plugin binary first."""
    try:
        ensure_node_class_loaded()
        nuke.createNode(NODE_CLASS_NAME)
    except Exception as error:
        nuke.tprint("[TVectorBlur] Unable to create node '{}': {}".format(NODE_CLASS_NAME, error))


def _create_menu():
    """Create the Nuke menu and add the command."""
    toolbar = nuke.menu("Nodes")
    menu = toolbar.findItem(MENU_NAME)
    if menu is None:
        menu = toolbar.addMenu(MENU_NAME, ICON_FILENAME)

    command_path = "{}/{}".format(MENU_NAME, NODE_CLASS_NAME)
    if toolbar.findItem(command_path) is None:
        callback = "import {} as _tn_menu; _tn_menu.create_node()".format(__name__)
        menu.addCommand(
            NODE_CLASS_NAME,
            callback,
            None,
            ICON_FILENAME,
        )


def add_menu():
    """Always create the menu; log plugin load state for diagnostics."""
    _add_menu_dependencies_to_plugin_path()
    _create_menu()

    if os.getenv(PLUGIN_LOADED_ENV_VAR) != "1":
        logger.warning("TVectorBlur menu created, but plugin binary is not loaded yet.")


def _add_menu_dependencies_to_plugin_path():
    resources_path = normalized_path(RESOURCES_PATH)
    if os.getenv(RESOURCE_PATH_ADDED_ENV_VAR) == resources_path:
        return

    nuke.pluginAppendPath(resources_path)
    os.environ[RESOURCE_PATH_ADDED_ENV_VAR] = resources_path
