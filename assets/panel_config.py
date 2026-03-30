#!/usr/bin/env python3
"""Prints JSON consumed by camster for panel button configuration."""

import json


def build_panel() -> dict:
    return {
        "buttons": [
            {"label": "Open", "action": "open"},
            {"label": "Export", "action": "export"},
            {"label": "Wireframe", "action": "toggle_wireframe"},
            {"label": "Normals", "action": "toggle_normals"},
            {"label": "Snap Front", "action": "snap", "argument": "front"},
            {"label": "Snap Right", "action": "snap", "argument": "right"},
            {"label": "Snap Top", "action": "snap", "argument": "top"},
            {"label": "Snap ISO", "action": "snap", "argument": "isometric"},
        ]
    }


if __name__ == "__main__":
    print(json.dumps(build_panel()))
