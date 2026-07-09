"""Node setup initializer."""

import os

import nuke  # ty:ignore[unresolved-import]

try:
    from TVectorBlur._consts import ICON_SANITIZER_SETUP_ENV_VAR, NODE_CLASS_NAME
except Exception:
    from _consts import ICON_SANITIZER_SETUP_ENV_VAR, NODE_CLASS_NAME


def _clear_template_node_icon():
    """Keep node icon empty so icon stays in menu shelf only."""
    node = nuke.thisNode()
    if node is None or node.Class() != NODE_CLASS_NAME:
        return

    icon_knob = node.knob("icon")
    if icon_knob is None:
        return

    if icon_knob.value():
        icon_knob.setValue("")


def setup_knob_changed():
    """Register one-time node setup hooks."""
    if os.getenv(ICON_SANITIZER_SETUP_ENV_VAR) == "1":
        return
    nuke.addOnCreate(_clear_template_node_icon, nodeClass=NODE_CLASS_NAME)  # ty:ignore[unresolved-attribute]
    os.environ[ICON_SANITIZER_SETUP_ENV_VAR] = "1"
