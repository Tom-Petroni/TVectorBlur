import os
import sys

import nuke


NODE_CLASS = os.environ.get("TSUITE_NODE_CLASS", "TVectorBlur")

print("NUKE", nuke.NUKE_VERSION_STRING)
print("NODE_CLASS", NODE_CLASS)
print("NUKE_PATH", os.environ.get("NUKE_PATH", ""))

if not hasattr(nuke.nodes, NODE_CLASS):
    raise RuntimeError(f"Node class '{NODE_CLASS}' is unavailable.")

src = nuke.nodes.Noise(name="SmokeSrc")
vec = nuke.nodes.Noise(name="SmokeVec")
blur = getattr(nuke.nodes, NODE_CLASS)(name="SmokeBlur")
blur.setInput(0, src)
blur.setInput(1, vec)

if "blur_distance" in blur.knobs():
    blur["blur_distance"].setValue(32)
if "blur_samples" in blur.knobs():
    blur["blur_samples"].setValue(16)

nuke.execute(blur, 1, 1)
print("SMOKE_OK")
sys.exit(0)
