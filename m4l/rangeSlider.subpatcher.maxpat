{
    "patcher": {
        "fileversion": 1,
        "appversion": {
            "major": 8,
            "minor": 6,
            "revision": 5,
            "architecture": "x64",
            "modernui": 1
        },
        "classnamespace": "box",
        "rect": [80.0, 80.0, 400.0, 240.0],
        "bglocked": 0,
        "openinpresentation": 1,
        "default_fontsize": 9.0,
        "default_fontface": 0,
        "default_fontname": "Andale Mono",
        "gridonopen": 1,
        "gridsize": [8.0, 8.0],
        "gridsnaponopen": 1,
        "objectsnaponopen": 1,
        "statusbarvisible": 2,
        "toolbarvisible": 1,
        "boxes": [
            {"box": {"id": "obj-inlet", "maxclass": "inlet", "comment": "setLo / setHi messages", "numinlets": 0, "numoutlets": 1, "outlettype": [""], "patching_rect": [16.0, 16.0, 30.0, 30.0]}},
            {"box": {"id": "obj-jsui", "maxclass": "jsui", "filename": "rangeSlider.jsui.js", "border": 0, "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [0.0, 0.0, 108.0, 32.0], "presentation": 1, "presentation_rect": [0.0, 0.0, 108.0, 32.0]}},
            {"box": {"id": "obj-outlet", "maxclass": "outlet", "comment": "rangeLo / rangeHi <int> from user drag", "numinlets": 1, "numoutlets": 0, "patching_rect": [340.0, 60.0, 30.0, 30.0]}}
        ],
        "lines": [
            {"patchline": {"source": ["obj-inlet", 0], "destination": ["obj-jsui", 0]}},
            {"patchline": {"source": ["obj-jsui", 0], "destination": ["obj-outlet", 0]}}
        ]
    }
}
