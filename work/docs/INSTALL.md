# Installation

## User install

1. Unzip the release package.
2. Copy the `TVectorBlur` folder into your `.nuke` directory.
3. If your setup does not already scan that folder automatically, add its path from your global `.nuke/init.py`.
4. Restart Nuke.

## Studio install

1. Put the `TVectorBlur` package on your central plugin share.
2. Add the package root to your studio Nuke package bootstrap.
3. Deploy the matching binary set for each supported Nuke version and operating system.

## Package layout

The runtime expects this structure:

```text
TVectorBlur/
  init.py
  menu.py
  _plugin_loader.py
  resources/
  bin/<major.minor>/<os>/
```

Examples:

- `bin/16.0/windows/TVectorBlur.dll`
- `bin/16.0/linux/TVectorBlur.so`

## Troubleshooting

- If the node does not appear, check the Nuke script editor for loader messages.
- If the binary does not load, verify that the package contains a matching `bin/<major.minor>/<os>` folder.
- Linux builds require a Linux Nuke SDK build environment. Windows binaries cannot be reused on Linux.
