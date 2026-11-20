# Dumps the marker tree of a RenderDoc capture: each region with its draw and dispatch counts.
# Run inside RenderDoc's embedded Python (CaptureRenderDoc.ps1 does this):
#   set LS_RDC_INPUT=capture.rdc & set LS_RDC_OUTPUT=markers.txt & qrenderdoc.exe --python RenderDocMarkers.py
import os

import renderdoc as rd


def count_draws(action):
    total = 1 if action.flags & rd.ActionFlags.Drawcall else 0
    for child in action.children:
        total += count_draws(child)
    return total


def write_tree(actions, depth, lines):
    for action in actions:
        if action.flags & rd.ActionFlags.PushMarker:
            lines.append("{}{}  [draws: {}]".format("  " * depth, action.customName, count_draws(action)))
            write_tree(action.children, depth + 1, lines)


def main():
    capture_path = os.environ["LS_RDC_INPUT"]
    output_path = os.environ["LS_RDC_OUTPUT"]
    lines = []
    capture = rd.OpenCaptureFile()
    try:
        result = capture.OpenFile(capture_path, "", None)
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot open capture: " + str(result))
        result, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if result != rd.ResultCode.Succeeded:
            raise RuntimeError("Cannot replay capture: " + str(result))
        try:
            roots = controller.GetRootActions()
            total = sum(count_draws(action) for action in roots)
            lines.append("capture: " + capture_path)
            lines.append("total draws: {}".format(total))
            write_tree(roots, 0, lines)
        finally:
            controller.Shutdown()
    except Exception as error:
        lines.append("ERROR: {}".format(error))
    finally:
        capture.Shutdown()
    with open(output_path, "w", encoding="utf-8") as file:
        file.write("\n".join(lines) + "\n")


main()
os._exit(0)
