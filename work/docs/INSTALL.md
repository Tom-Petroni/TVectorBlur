# Install

## What to Ship

The folder to install into `.nuke` is the package under `publish/`.

Typical payload:

```text
publish/
  init.py
  <PluginName>/
    init.py
    menu.py
    resources/
    bin/
      <version>/<os>/<arch>/<binary>
```

## End User Install

1. Copy `publish/init.py` into the user's `.nuke` directory.
2. Copy the `publish/<PluginName>/` folder into the same `.nuke` directory.
3. Restart Nuke.

Optional fallback in `.nuke/init.py`:

```python
import nuke
nuke.pluginAddPath("./<PluginName>")
```

## Verify

After launch:

- the plugin package should be discoverable by Nuke
- the node class should load without missing binary errors
- the expected menu entry should appear

If loading fails, first verify that the correct binary exists under:

`<PluginName>/bin/<nuke_version>/<os>/<arch>/`
