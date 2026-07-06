"""Menu entry point for TVectorBlur."""

import logging

try:
    from TVectorBlur._menu_creator import add_menu
except Exception:
    from _menu_creator import add_menu

logger = logging.getLogger(__name__)

try:
    add_menu()
except Exception:  # pragma: no cover
    logger.exception("Unexpected failure while creating the TVectorBlur menu.")
