#!/usr/bin/env python3
"""Customize the drastic hook menu in drastic_video.c.

Changes:
1. Version display: "NDS r2.5.2.0" → "Advanced NDS"
2. Exit text: "Exit DraStic-trngaje" → "Exit DraStic"
3. Center savestate screenshot (instead of right-aligned)
"""
import sys


def fix_menu(content):
    # Fix version display
    # Original: sprintf(buf, "NDS %s", &p->msg[8]);
    # This takes "Version r2.5.2.0" → skips "Version " (8 chars) → "r2.5.2.0" → "NDS r2.5.2.0"
    # Change to show "Advanced NDS"
    old_version = 'sprintf(buf, "NDS %s", &p->msg[8]);'
    new_version = 'sprintf(buf, "Advanced NDS");'
    if old_version in content:
        content = content.replace(old_version, new_version)
        print("fix-menu: changed version display to 'Advanced NDS'")
    else:
        print("fix-menu: WARNING - could not find version sprintf")

    # Fix exit text reference
    old_exit = 'Exit DraStic-trngaje'
    new_exit = 'Exit DraStic'
    content = content.replace(old_exit, new_exit)
    print("fix-menu: cleaned up exit text")

    # Move savestate screenshot to right of menu area (was far-right)
    old_shot_x = 'g_advdrastic.iDisplay_width - (NDS_W + g_advdrastic.iDisplay_width * 10 / 640)'
    new_shot_x = 'g_advdrastic.iDisplay_width - g_advdrastic.iDisplay_width * 90 / 640 - NDS_W'
    count = content.count(old_shot_x)
    if count > 0:
        content = content.replace(old_shot_x, new_shot_x)
        print(f"fix-menu: repositioned savestate screenshot ({count} occurrences)")
    # Also handle variant with extra space before * 10
    old_shot_x2 = 'g_advdrastic.iDisplay_width - (NDS_W + g_advdrastic.iDisplay_width  * 10 / 640)'
    count2 = content.count(old_shot_x2)
    if count2 > 0:
        content = content.replace(old_shot_x2, new_shot_x)
        print(f"fix-menu: repositioned savestate screenshot variant ({count2} occurrences)")

    return content


if __name__ == '__main__':
    filepath = sys.argv[1]
    with open(filepath, 'r') as f:
        content = f.read()
    content = fix_menu(content)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"fix-menu: wrote {filepath}")
