"""Main entry point for TVectorBlurCUDA Nuke plugin."""

import logging
import os

import nuke  # ty:ignore[unresolved-import]

try:
    from TVectorBlurCUDA._consts import HOOKS_SETUP_ENV_VAR
    from TVectorBlurCUDA._node_setup import setup_node_hooks
    from TVectorBlurCUDA._plugin_loader import add_plugin_path_safe
except Exception:
    from _consts import HOOKS_SETUP_ENV_VAR
    from _node_setup import setup_node_hooks
    from _plugin_loader import add_plugin_path_safe

logger = logging.getLogger(__name__)


def _refresh_plugin_path() -> None:
    loaded = add_plugin_path_safe()
    if loaded:
        setup_node_hooks()
    else:
        nuke.tprint("[TVectorBlurCUDA] Plugin binary not loaded yet.")


def _register_script_hooks() -> None:
    if os.getenv(HOOKS_SETUP_ENV_VAR) == "1":
        return

    for hook_name in ("addBeforeScriptLoad", "addOnScriptNew", "addOnScriptLoad"):
        hook = getattr(nuke, hook_name, None)
        if callable(hook):
            hook(_refresh_plugin_path)

    os.environ[HOOKS_SETUP_ENV_VAR] = "1"


try:
    _register_script_hooks()
    _refresh_plugin_path()
except Exception:  # pragma: no cover
    logger.exception("Unexpected failure while initializing the TVectorBlurCUDA plugin.")
