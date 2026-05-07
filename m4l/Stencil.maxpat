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
        "rect": [80.0, 80.0, 1400.0, 820.0],
        "bglocked": 0,
        "openinpresentation": 1,
        "devicewidth": 1000.0,
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

            {"box": {"id": "obj-grp-generate", "maxclass": "panel", "bgcolor": [0, 0, 0, 0], "bordercolor": [0.929, 0.910, 0.863, 0.18], "border": 1, "rounded": 0, "numinlets": 1, "numoutlets": 0, "patching_rect": [40.0, 60.0, 200.0, 12.0], "presentation": 1, "presentation_rect": [8.0, 8.0, 316.0, 164.0]}},
            {"box": {"id": "obj-grp-register", "maxclass": "panel", "bgcolor": [0, 0, 0, 0], "bordercolor": [0.929, 0.910, 0.863, 0.18], "border": 1, "rounded": 0, "numinlets": 1, "numoutlets": 0, "patching_rect": [240.0, 60.0, 200.0, 12.0], "presentation": 1, "presentation_rect": [332.0, 8.0, 336.0, 164.0]}},
            {"box": {"id": "obj-grp-io", "maxclass": "panel", "bgcolor": [0, 0, 0, 0], "bordercolor": [0.929, 0.910, 0.863, 0.18], "border": 1, "rounded": 0, "numinlets": 1, "numoutlets": 0, "patching_rect": [440.0, 60.0, 200.0, 12.0], "presentation": 1, "presentation_rect": [676.0, 8.0, 316.0, 164.0]}},

            {"box": {"id": "obj-lgnd-generate", "maxclass": "comment", "text": "GENERATE", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 8.0, "textcolor": [0.471, 0.471, 0.271, 1.0], "patching_rect": [40.0, 60.0, 80.0, 12.0], "presentation": 1, "presentation_rect": [20.0, 14.0, 70.0, 12.0]}},
            {"box": {"id": "obj-lgnd-register", "maxclass": "comment", "text": "REGISTER", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 8.0, "textcolor": [0.471, 0.471, 0.271, 1.0], "patching_rect": [240.0, 60.0, 80.0, 12.0], "presentation": 1, "presentation_rect": [344.0, 14.0, 70.0, 12.0]}},
            {"box": {"id": "obj-lgnd-io", "maxclass": "comment", "text": "I/O", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 8.0, "textcolor": [0.471, 0.471, 0.271, 1.0], "patching_rect": [440.0, 60.0, 80.0, 12.0], "presentation": 1, "presentation_rect": [688.0, 14.0, 30.0, 12.0]}},

            {"box": {"id": "obj-thisdevice", "maxclass": "newobj", "text": "live.thisdevice", "numinlets": 1, "numoutlets": 3, "outlettype": ["bang", "bang", ""], "patching_rect": [40.0, 60.0, 102.0, 22.0]}},
            {"box": {"id": "obj-loadbang", "maxclass": "newobj", "text": "loadbang", "numinlets": 1, "numoutlets": 1, "outlettype": ["bang"], "patching_rect": [40.0, 90.0, 65.0, 22.0]}},
            {"box": {"id": "obj-msg-getid", "maxclass": "message", "text": "getid", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [40.0, 120.0, 50.0, 22.0]}},
            {"box": {"id": "obj-livepath", "maxclass": "newobj", "text": "live.path live_set", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", ""], "patching_rect": [40.0, 150.0, 120.0, 22.0]}},
            {"box": {"id": "obj-liveobs", "maxclass": "newobj", "text": "live.observer is_playing", "numinlets": 2, "numoutlets": 3, "outlettype": ["", "", ""], "patching_rect": [40.0, 180.0, 160.0, 22.0]}},

            {"box": {"id": "obj-sel-playing", "maxclass": "newobj", "text": "sel 0 1", "numinlets": 1, "numoutlets": 3, "outlettype": ["bang", "bang", ""], "patching_rect": [40.0, 210.0, 50.0, 22.0]}},
            {"box": {"id": "obj-msg-tstart", "maxclass": "message", "text": "transportStart", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [110.0, 240.0, 110.0, 22.0]}},
            {"box": {"id": "obj-msg-tstop", "maxclass": "message", "text": "transportStop", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [40.0, 240.0, 110.0, 22.0]}},

            {"box": {"id": "obj-metro", "maxclass": "newobj", "text": "qmetro 16n @quantize 16n @active 0", "numinlets": 2, "numoutlets": 1, "outlettype": ["bang"], "patching_rect": [240.0, 210.0, 220.0, 22.0]}},
            {"box": {"id": "obj-counter", "maxclass": "newobj", "text": "counter", "numinlets": 5, "numoutlets": 4, "outlettype": ["int", "int", "int", "int"], "patching_rect": [240.0, 240.0, 55.0, 22.0]}},
            {"box": {"id": "obj-prep-step", "maxclass": "newobj", "text": "prepend step", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [240.0, 270.0, 90.0, 22.0]}},

            {"box": {"id": "obj-panic-btn", "maxclass": "live.text", "numinlets": 1, "numoutlets": 1, "outlettype": ["bang"], "text": "PANIC", "fontname": "Andale Mono", "fontsize": 8.0, "patching_rect": [500.0, 60.0, 60.0, 16.0]}},
            {"box": {"id": "obj-msg-panic", "maxclass": "message", "text": "panic", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [500.0, 90.0, 50.0, 22.0]}},

            {"box": {"id": "obj-midiin", "maxclass": "newobj", "text": "midiin", "numinlets": 1, "numoutlets": 1, "outlettype": ["int"], "patching_rect": [600.0, 60.0, 45.0, 22.0]}},
            {"box": {"id": "obj-midiparse", "maxclass": "newobj", "text": "midiparse", "numinlets": 1, "numoutlets": 7, "outlettype": ["", "", "", "int", "int", "int", "int"], "patching_rect": [600.0, 90.0, 80.0, 22.0]}},
            {"box": {"id": "obj-unpack-mp", "maxclass": "newobj", "text": "unpack 0 0", "numinlets": 1, "numoutlets": 2, "outlettype": ["int", "int"], "patching_rect": [600.0, 120.0, 80.0, 22.0]}},
            {"box": {"id": "obj-pak-noteargs", "maxclass": "newobj", "text": "pak 0 0 0", "numinlets": 3, "numoutlets": 1, "outlettype": [""], "patching_rect": [600.0, 150.0, 80.0, 22.0]}},
            {"box": {"id": "obj-if-noteinout", "maxclass": "newobj", "text": "if $i2 > 0 then noteIn $i1 $i2 $i3 else noteOff $i1 $i3", "numinlets": 3, "numoutlets": 1, "outlettype": [""], "patching_rect": [600.0, 180.0, 380.0, 22.0]}},

            {"box": {"id": "obj-nodescript", "maxclass": "newobj", "text": "node.script stencil.mjs @autostart 1", "numinlets": 1, "numoutlets": 2, "outlettype": ["", ""], "patching_rect": [40.0, 360.0, 420.0, 22.0]}},
            {"box": {"id": "obj-print-from-node", "maxclass": "newobj", "text": "print stencil-from-node", "numinlets": 1, "numoutlets": 0, "patching_rect": [40.0, 390.0, 200.0, 22.0]}},

            {"box": {"id": "obj-route-out", "maxclass": "newobj", "text": "route note ready register ringHead triggerFlash", "numinlets": 1, "numoutlets": 6, "outlettype": ["", "", "", "", "", ""], "patching_rect": [240.0, 390.0, 320.0, 22.0]}},
            {"box": {"id": "obj-trig-ready", "maxclass": "newobj", "text": "t b", "numinlets": 1, "numoutlets": 1, "outlettype": ["bang"], "patching_rect": [340.0, 450.0, 40.0, 22.0]}},
            {"box": {"id": "obj-unpack-note", "maxclass": "newobj", "text": "unpack 0 0 0", "numinlets": 1, "numoutlets": 3, "outlettype": ["int", "int", "int"], "patching_rect": [240.0, 420.0, 100.0, 22.0]}},
            {"box": {"id": "obj-noteout", "maxclass": "newobj", "text": "noteout", "numinlets": 3, "numoutlets": 0, "patching_rect": [240.0, 480.0, 60.0, 22.0]}},
            {"box": {"id": "obj-defer-reg", "maxclass": "newobj", "text": "deferlow", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [400.0, 390.0, 80.0, 22.0]}},
            {"box": {"id": "obj-defer-pos", "maxclass": "newobj", "text": "deferlow", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [520.0, 390.0, 80.0, 22.0]}},
            {"box": {"id": "obj-defer-flash", "maxclass": "newobj", "text": "deferlow", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [640.0, 390.0, 80.0, 22.0]}},
            {"box": {"id": "obj-prep-reg", "maxclass": "newobj", "text": "prepend register", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [400.0, 420.0, 110.0, 22.0]}},
            {"box": {"id": "obj-prep-pos", "maxclass": "newobj", "text": "prepend ringHead", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [520.0, 420.0, 110.0, 22.0]}},
            {"box": {"id": "obj-prep-flash", "maxclass": "newobj", "text": "prepend triggerFlash", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [640.0, 420.0, 130.0, 22.0]}},

            {"box": {"id": "obj-bpatcher-ring", "maxclass": "bpatcher", "name": "registerRing.subpatcher.maxpat", "border": 0, "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [344.0, 32.0, 312.0, 136.0], "presentation": 1, "presentation_rect": [344.0, 32.0, 312.0, 136.0]}},


            {"box": {"id": "obj-lbl-length", "maxclass": "comment", "text": "LEN", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 60.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [24.0, 38.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-length", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 60.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [76.0, 36.0, 60.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [8], "parameter_initial_enable": 1, "parameter_longname": "StencilTmLength", "parameter_mmax": 32.0, "parameter_mmin": 2.0, "parameter_shortname": "Len", "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-length", "maxclass": "newobj", "text": "prepend setParam length", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 60.0, 180.0, 22.0]}},

            {"box": {"id": "obj-w-lock", "maxclass": "live.dial", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 90.0, 36.0, 52.0], "presentation": 1, "presentation_rect": [88.0, 104.0, 36.0, 52.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [0.5], "parameter_initial_enable": 1, "parameter_longname": "StencilTmLock", "parameter_mmax": 1.0, "parameter_mmin": 0.0, "parameter_shortname": "Lock", "parameter_type": 0, "parameter_unitstyle": 1}}}},
            {"box": {"id": "obj-prep-lock", "maxclass": "newobj", "text": "prepend setParam lock", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 90.0, 180.0, 22.0]}},

            {"box": {"id": "obj-w-density", "maxclass": "live.dial", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 120.0, 36.0, 52.0], "presentation": 1, "presentation_rect": [192.0, 104.0, 36.0, 52.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [1.0], "parameter_initial_enable": 1, "parameter_longname": "StencilTmDensity", "parameter_mmax": 1.0, "parameter_mmin": 0.0, "parameter_shortname": "Dens", "parameter_type": 0, "parameter_unitstyle": 1}}}},
            {"box": {"id": "obj-prep-density", "maxclass": "newobj", "text": "prepend setParam density", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 120.0, 180.0, 22.0]}},

            {"box": {"id": "obj-lbl-rangeLo", "maxclass": "comment", "text": "LO", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 150.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [24.0, 60.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-rangeLo", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 150.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [76.0, 58.0, 60.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [48], "parameter_initial_enable": 1, "parameter_longname": "StencilTmRangeLo", "parameter_mmax": 127.0, "parameter_mmin": 0.0, "parameter_shortname": "RngLo", "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-rangeLo", "maxclass": "newobj", "text": "prepend setParam rangeLo", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 150.0, 180.0, 22.0]}},

            {"box": {"id": "obj-lbl-rangeHi", "maxclass": "comment", "text": "HI", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 180.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [148.0, 60.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-rangeHi", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 180.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [200.0, 58.0, 60.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [72], "parameter_initial_enable": 1, "parameter_longname": "StencilTmRangeHi", "parameter_mmax": 127.0, "parameter_mmin": 0.0, "parameter_shortname": "RngHi", "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-rangeHi", "maxclass": "newobj", "text": "prepend setParam rangeHi", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 180.0, 180.0, 22.0]}},

            {"box": {"id": "obj-lbl-mode", "maxclass": "comment", "text": "MODE", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 215.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [24.0, 82.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-mode", "maxclass": "live.menu", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"], "parameter_enable": 1, "patching_rect": [970.0, 215.0, 100.0, 22.0], "presentation": 1, "presentation_rect": [76.0, 80.0, 84.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_enum": ["note", "gate", "velocity"], "parameter_initial": [0], "parameter_initial_enable": 1, "parameter_longname": "StencilTmMode", "parameter_shortname": "Mode", "parameter_type": 2}}}},
            {"box": {"id": "obj-sel-mode", "maxclass": "newobj", "text": "sel 0 1 2", "numinlets": 1, "numoutlets": 4, "outlettype": ["bang", "bang", "bang", ""], "patching_rect": [1110.0, 215.0, 80.0, 22.0]}},
            {"box": {"id": "obj-msg-mode-note", "maxclass": "message", "text": "setParam mode note", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 245.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-mode-gate", "maxclass": "message", "text": "setParam mode gate", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 275.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-mode-velocity", "maxclass": "message", "text": "setParam mode velocity", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 305.0, 200.0, 22.0]}},

            {"box": {"id": "obj-lbl-subdivision", "maxclass": "comment", "text": "SUBD", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 210.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [172.0, 82.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-subdivision", "maxclass": "live.menu", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"], "parameter_enable": 1, "patching_rect": [970.0, 210.0, 130.0, 22.0], "presentation": 1, "presentation_rect": [220.0, 80.0, 80.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_enum": ["8th", "16th", "32nd", "8T", "16T"], "parameter_initial": [1], "parameter_initial_enable": 1, "parameter_longname": "StencilTmSubdivision", "parameter_shortname": "Subdiv", "parameter_type": 2}}}},
            {"box": {"id": "obj-sel-subdivision", "maxclass": "newobj", "text": "sel 0 1 2 3 4", "numinlets": 1, "numoutlets": 6, "outlettype": ["bang", "bang", "bang", "bang", "bang", ""], "patching_rect": [1110.0, 210.0, 100.0, 22.0]}},
            {"box": {"id": "obj-msg-subdivision-8th", "maxclass": "message", "text": "setParam subdivision 8th", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 240.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-subdivision-16th", "maxclass": "message", "text": "setParam subdivision 16th", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 270.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-subdivision-32nd", "maxclass": "message", "text": "setParam subdivision 32nd", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 300.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-subdivision-8T", "maxclass": "message", "text": "setParam subdivision 8T", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 330.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-subdivision-16T", "maxclass": "message", "text": "setParam subdivision 16T", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 360.0, 200.0, 22.0]}},

            {"box": {"id": "obj-lbl-triggerMode", "maxclass": "comment", "text": "TRG", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 240.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [692.0, 38.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-triggerMode", "maxclass": "live.menu", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"], "parameter_enable": 1, "patching_rect": [970.0, 240.0, 130.0, 22.0], "presentation": 1, "presentation_rect": [744.0, 36.0, 240.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_enum": ["auto", "gate", "seed"], "parameter_initial": [0], "parameter_initial_enable": 1, "parameter_longname": "StencilTmTriggerMode", "parameter_shortname": "Trig", "parameter_type": 2}}}},
            {"box": {"id": "obj-sel-triggerMode", "maxclass": "newobj", "text": "sel 0 1 2", "numinlets": 1, "numoutlets": 4, "outlettype": ["bang", "bang", "bang", ""], "patching_rect": [1110.0, 240.0, 80.0, 22.0]}},
            {"box": {"id": "obj-msg-triggerMode-auto", "maxclass": "message", "text": "setParam triggerMode auto", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 270.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-triggerMode-gate", "maxclass": "message", "text": "setParam triggerMode gate", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 300.0, 200.0, 22.0]}},
            {"box": {"id": "obj-msg-triggerMode-seed", "maxclass": "message", "text": "setParam triggerMode seed", "numinlets": 2, "numoutlets": 1, "outlettype": [""], "patching_rect": [1110.0, 330.0, 200.0, 22.0]}},

            {"box": {"id": "obj-lbl-inputChannel", "maxclass": "comment", "text": "IN", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 270.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [692.0, 64.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-inputChannel", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 270.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [744.0, 62.0, 50.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [0], "parameter_initial_enable": 1, "parameter_longname": "StencilTmInputChannel", "parameter_mmax": 16.0, "parameter_mmin": 0.0, "parameter_shortname": "InCh", "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-inputChannel", "maxclass": "newobj", "text": "prepend setParam inputChannel", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 270.0, 200.0, 22.0]}},

            {"box": {"id": "obj-lbl-outputVelocity", "maxclass": "comment", "text": "VEL", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 300.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [692.0, 94.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-outputVelocity", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 300.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [744.0, 92.0, 60.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [100], "parameter_initial_enable": 1, "parameter_longname": "StencilTmOutputVelocity", "parameter_mmax": 127.0, "parameter_mmin": 1.0, "parameter_shortname": "OutVel", "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-outputVelocity", "maxclass": "newobj", "text": "prepend setParam outputVelocity", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 300.0, 220.0, 22.0]}},

            {"box": {"id": "obj-w-outputGate", "maxclass": "live.dial", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 330.0, 36.0, 52.0], "presentation": 1, "presentation_rect": [820.0, 110.0, 36.0, 52.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [0.5], "parameter_initial_enable": 1, "parameter_longname": "StencilTmOutputGate", "parameter_mmax": 1.0, "parameter_mmin": 0.0, "parameter_shortname": "OutGt", "parameter_type": 0, "parameter_unitstyle": 1}}}},
            {"box": {"id": "obj-prep-outputGate", "maxclass": "newobj", "text": "prepend setParam outputGate", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 330.0, 180.0, 22.0]}},

            {"box": {"id": "obj-lbl-outputChannel", "maxclass": "comment", "text": "OUT", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 360.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [834.0, 64.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-outputChannel", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 360.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [884.0, 62.0, 50.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [1], "parameter_initial_enable": 1, "parameter_longname": "StencilTmOutputChannel", "parameter_mmax": 16.0, "parameter_mmin": 1.0, "parameter_shortname": "OutCh", "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-outputChannel", "maxclass": "newobj", "text": "prepend setParam outputChannel", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 360.0, 220.0, 22.0]}},

            {"box": {"id": "obj-lbl-seed", "maxclass": "comment", "text": "SEED", "numinlets": 1, "numoutlets": 0, "fontname": "Andale Mono", "fontsize": 9.0, "textcolor": [0.929, 0.910, 0.863, 0.85], "patching_rect": [970.0, 390.0, 60.0, 14.0], "presentation": 1, "presentation_rect": [834.0, 94.0, 40.0, 14.0]}},
            {"box": {"id": "obj-w-seed", "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"], "parameter_enable": 1, "patching_rect": [970.0, 390.0, 60.0, 22.0], "presentation": 1, "presentation_rect": [884.0, 92.0, 80.0, 16.0], "saved_attribute_attributes": {"valueof": {"parameter_initial": [42], "parameter_initial_enable": 1, "parameter_longname": "StencilTmSeed", "parameter_mmax": 2147483647.0, "parameter_mmin": 0.0, "parameter_shortname": "Seed", "parameter_steps": 2147483648, "parameter_type": 1, "parameter_unitstyle": 0}}}},
            {"box": {"id": "obj-prep-seed", "maxclass": "newobj", "text": "prepend setParam seed", "numinlets": 1, "numoutlets": 1, "outlettype": [""], "patching_rect": [1100.0, 390.0, 180.0, 22.0]}}

        ],
        "lines": [

            {"patchline": {"source": ["obj-thisdevice", 0], "destination": ["obj-msg-getid", 0]}},
            {"patchline": {"source": ["obj-loadbang", 0], "destination": ["obj-msg-getid", 0]}},
            {"patchline": {"source": ["obj-msg-getid", 0], "destination": ["obj-livepath", 0]}},
            {"patchline": {"source": ["obj-livepath", 0], "destination": ["obj-liveobs", 0]}},

            {"patchline": {"source": ["obj-liveobs", 0], "destination": ["obj-metro", 0]}},
            {"patchline": {"source": ["obj-liveobs", 0], "destination": ["obj-sel-playing", 0]}},
            {"patchline": {"source": ["obj-sel-playing", 0], "destination": ["obj-msg-tstop", 0]}},
            {"patchline": {"source": ["obj-sel-playing", 1], "destination": ["obj-msg-tstart", 0]}},
            {"patchline": {"source": ["obj-msg-tstop", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-tstart", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-metro", 0], "destination": ["obj-counter", 0]}},
            {"patchline": {"source": ["obj-counter", 0], "destination": ["obj-prep-step", 0]}},
            {"patchline": {"source": ["obj-prep-step", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-panic-btn", 0], "destination": ["obj-msg-panic", 0]}},
            {"patchline": {"source": ["obj-msg-panic", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-midiin", 0], "destination": ["obj-midiparse", 0]}},
            {"patchline": {"source": ["obj-midiparse", 0], "destination": ["obj-unpack-mp", 0]}},
            {"patchline": {"source": ["obj-midiparse", 4], "destination": ["obj-pak-noteargs", 2]}},
            {"patchline": {"source": ["obj-unpack-mp", 1], "destination": ["obj-pak-noteargs", 1]}},
            {"patchline": {"source": ["obj-unpack-mp", 0], "destination": ["obj-pak-noteargs", 0]}},
            {"patchline": {"source": ["obj-pak-noteargs", 0], "destination": ["obj-if-noteinout", 0]}},
            {"patchline": {"source": ["obj-if-noteinout", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-nodescript", 0], "destination": ["obj-print-from-node", 0]}},
            {"patchline": {"source": ["obj-nodescript", 0], "destination": ["obj-route-out", 0]}},


            {"patchline": {"source": ["obj-route-out", 0], "destination": ["obj-unpack-note", 0]}},
            {"patchline": {"source": ["obj-unpack-note", 0], "destination": ["obj-noteout", 0]}},
            {"patchline": {"source": ["obj-unpack-note", 1], "destination": ["obj-noteout", 1]}},
            {"patchline": {"source": ["obj-unpack-note", 2], "destination": ["obj-noteout", 2]}},


            {"patchline": {"source": ["obj-route-out", 2], "destination": ["obj-defer-reg", 0]}},
            {"patchline": {"source": ["obj-defer-reg", 0], "destination": ["obj-prep-reg", 0]}},
            {"patchline": {"source": ["obj-prep-reg", 0], "destination": ["obj-bpatcher-ring", 0]}},
            {"patchline": {"source": ["obj-route-out", 3], "destination": ["obj-defer-pos", 0]}},
            {"patchline": {"source": ["obj-defer-pos", 0], "destination": ["obj-prep-pos", 0]}},
            {"patchline": {"source": ["obj-prep-pos", 0], "destination": ["obj-bpatcher-ring", 0]}},
            {"patchline": {"source": ["obj-route-out", 4], "destination": ["obj-defer-flash", 0]}},
            {"patchline": {"source": ["obj-defer-flash", 0], "destination": ["obj-prep-flash", 0]}},
            {"patchline": {"source": ["obj-prep-flash", 0], "destination": ["obj-bpatcher-ring", 0]}},

            {"patchline": {"source": ["obj-bpatcher-ring", 0], "destination": ["obj-nodescript", 0]}},


            {"patchline": {"source": ["obj-w-length", 0], "destination": ["obj-prep-length", 0]}},
            {"patchline": {"source": ["obj-prep-length", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-lock", 0], "destination": ["obj-prep-lock", 0]}},
            {"patchline": {"source": ["obj-prep-lock", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-rangeLo", 0], "destination": ["obj-prep-rangeLo", 0]}},
            {"patchline": {"source": ["obj-prep-rangeLo", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-rangeHi", 0], "destination": ["obj-prep-rangeHi", 0]}},
            {"patchline": {"source": ["obj-prep-rangeHi", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-density", 0], "destination": ["obj-prep-density", 0]}},
            {"patchline": {"source": ["obj-prep-density", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-mode", 0], "destination": ["obj-sel-mode", 0]}},
            {"patchline": {"source": ["obj-sel-mode", 0], "destination": ["obj-msg-mode-note", 0]}},
            {"patchline": {"source": ["obj-sel-mode", 1], "destination": ["obj-msg-mode-gate", 0]}},
            {"patchline": {"source": ["obj-sel-mode", 2], "destination": ["obj-msg-mode-velocity", 0]}},
            {"patchline": {"source": ["obj-msg-mode-note", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-mode-gate", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-mode-velocity", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-subdivision", 0], "destination": ["obj-sel-subdivision", 0]}},
            {"patchline": {"source": ["obj-sel-subdivision", 0], "destination": ["obj-msg-subdivision-8th", 0]}},
            {"patchline": {"source": ["obj-sel-subdivision", 1], "destination": ["obj-msg-subdivision-16th", 0]}},
            {"patchline": {"source": ["obj-sel-subdivision", 2], "destination": ["obj-msg-subdivision-32nd", 0]}},
            {"patchline": {"source": ["obj-sel-subdivision", 3], "destination": ["obj-msg-subdivision-8T", 0]}},
            {"patchline": {"source": ["obj-sel-subdivision", 4], "destination": ["obj-msg-subdivision-16T", 0]}},
            {"patchline": {"source": ["obj-msg-subdivision-8th", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-subdivision-16th", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-subdivision-32nd", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-subdivision-8T", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-subdivision-16T", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-seed", 0], "destination": ["obj-prep-seed", 0]}},
            {"patchline": {"source": ["obj-prep-seed", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-triggerMode", 0], "destination": ["obj-sel-triggerMode", 0]}},
            {"patchline": {"source": ["obj-sel-triggerMode", 0], "destination": ["obj-msg-triggerMode-auto", 0]}},
            {"patchline": {"source": ["obj-sel-triggerMode", 1], "destination": ["obj-msg-triggerMode-gate", 0]}},
            {"patchline": {"source": ["obj-sel-triggerMode", 2], "destination": ["obj-msg-triggerMode-seed", 0]}},
            {"patchline": {"source": ["obj-msg-triggerMode-auto", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-triggerMode-gate", 0], "destination": ["obj-nodescript", 0]}},
            {"patchline": {"source": ["obj-msg-triggerMode-seed", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-inputChannel", 0], "destination": ["obj-prep-inputChannel", 0]}},
            {"patchline": {"source": ["obj-prep-inputChannel", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-outputVelocity", 0], "destination": ["obj-prep-outputVelocity", 0]}},
            {"patchline": {"source": ["obj-prep-outputVelocity", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-outputGate", 0], "destination": ["obj-prep-outputGate", 0]}},
            {"patchline": {"source": ["obj-prep-outputGate", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-w-outputChannel", 0], "destination": ["obj-prep-outputChannel", 0]}},
            {"patchline": {"source": ["obj-prep-outputChannel", 0], "destination": ["obj-nodescript", 0]}},

            {"patchline": {"source": ["obj-route-out", 1], "destination": ["obj-trig-ready", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-length", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-lock", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-density", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-rangeLo", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-rangeHi", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-subdivision", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-mode", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-triggerMode", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-inputChannel", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-outputVelocity", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-outputGate", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-outputChannel", 0]}},
            {"patchline": {"source": ["obj-trig-ready", 0], "destination": ["obj-w-seed", 0]}}

        ]
    }
}
