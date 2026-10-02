#!/usr/bin/env python3
"""check-tray-icon.py — проверка контрастности иконки SmartClip в трее.

Читает IconPixmap запущенного SmartClip через D-Bus (StatusNotifierItem),
определяет фактический цвет значка (светлый/тёмный) и сверяет с темой
верхней панели GNOME (org.gnome.desktop.interface color-scheme).

Ожидание: тёмная панель → СВЕТЛАЯ иконка; светлая панель → ТЁМНАЯ иконка.

Использование:
  python3 tools/check-tray-icon.py            # авто-ожидание по теме панели
  python3 tools/check-tray-icon.py --expect dark   # ожидать тёмную иконку
  python3 tools/check-tray-icon.py --expect light  # ожидать светлую иконку
  python3 tools/check-tray-icon.py -v         # подробный вывод

Код возврата: 0 — совпало, 1 — не совпало/ошибка.
"""
from __future__ import annotations

import argparse
import subprocess
import sys

SNI_IFACE = "org.kde.StatusNotifierItem"
WATCHER = "org.kde.StatusNotifierWatcher"


def sh(*args: str) -> str:
    try:
        return subprocess.run(args, capture_output=True, text=True,
                              timeout=5).stdout.strip()
    except Exception:
        return ""


def panel_expected() -> str:
    """'dark' | 'light' — какой цвет панели сейчас у GNOME."""
    cs = sh("gsettings", "get", "org.gnome.desktop.interface", "color-scheme")
    if "prefer-light" in cs:
        return "light"
    if "prefer-dark" in cs:
        return "dark"
    # 'default' → тёмная панель (вариант gnome-shell-dark.css, #000)
    return "dark"


def _sni_items():
    """Список (bus_name, object_path) из StatusNotifierWatcher."""
    try:
        out = sh("busctl", "--user", "call", WATCHER, "/StatusNotifierWatcher",
                 "org.freedesktop.DBus.Properties", "Get", "ss", WATCHER,
                 "RegisteredStatusNotifierItems")
    except Exception:
        out = ""
    import re
    items = re.findall(r'"(:[\d.]+)@([^"]+)"', out)
    return items


def _pid_of(bus_name: str) -> int:
    out = sh("busctl", "--user", "call", "org.freedesktop.DBus",
             "/org/freedesktop/DBus", "org.freedesktop.DBus",
             "GetConnectionUnixProcessID", "s", bus_name)
    import re
    m = re.search(r"\b(\d+)\b", out)
    return int(m.group(1)) if m else -1


def _smartclip_pid() -> int:
    out = sh("pgrep", "-f", "SmartClip")
    for line in out.splitlines():
        try:
            return int(line.strip())
        except ValueError:
            continue
    return -1


def icon_body_color(bus_name: str, path: str, verbose: bool):
    """Средний цвет «тела» иконки из IconPixmap. None — не нашли."""
    import re
    out = sh("busctl", "--user", "call", bus_name, path,
             "org.freedesktop.DBus.Properties", "Get", "ss", SNI_IFACE,
             "IconPixmap")
    m = re.search(r"a\(iiay\)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9 ]+)", out)
    if m:
        # формат: a(iiay) <кол-во> <ширина> <высота> <байт> <пиксели...>
        w, h, ln = int(m.group(2)), int(m.group(3)), int(m.group(4))
        nums = [int(x) for x in m.group(5).split()][: w * h * 4]
    else:
        w = h = 0
        nums = []
    px = [(nums[i + 1], nums[i + 2], nums[i + 3], nums[i])
          for i in range(0, len(nums), 4)]      # ARGB
    # «Тело» = непрозрачные пиксели. НЕ фильтруем по яркости: иначе чёрная
    # иконка (RGB 0,0,0) отбрасывается и получить её цвет невозможно.
    body = [p for p in px if p[3] > 40]
    if not body:
        return None, w, h
    r = sum(p[0] for p in body) / len(body)
    g = sum(p[1] for p in body) / len(body)
    b = sum(p[2] for p in body) / len(body)
    if verbose:
        print(f"  пиксмап {w}x{h}, непрозрачных {len(body)} "
              f"(из {len(px)}), средний цвет RGB({r:.0f},{g:.0f},{b:.0f})")
    return (r, g, b), w, h


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--expect", choices=["auto", "light", "dark"],
                    default="auto")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    pid = _smartclip_pid()
    if pid < 0:
        print("SmartClip не запущен (pgrep пусто)")
        return 1

    items = _sni_items()
    target = None
    for name, path in items:
        if _pid_of(name) == pid:
            target = (name, path)
            break
    if target is None:
        print(f"SNI-элемент SmartClip (pid {pid}) не найден. "
              f"В трее: {items}")
        return 1
    bus_name, path = target
    if a.verbose:
        print(f"SmartClip pid={pid}, bus={bus_name}, path={path}")

    color, w, h = icon_body_color(bus_name, path, a.verbose)
    if color is None:
        print("IconPixmap не прочитан (иконку трей мог не забрать)")
        return 1

    avg = sum(color) / 3.0
    icon = "light" if avg > 170 else ("dark" if avg < 85 else "mid")
    exp = panel_expected() if a.expect == "auto" else a.expect
    want_icon = "light" if exp == "dark" else "dark"

    ok = (icon == want_icon)
    print(f"панель: {exp}   иконка: {icon} "
          f"(RGB {color[0]:.0f},{color[1]:.0f},{color[2]:.0f})   "
          f"ожидалось: {want_icon}   {'OK ✅' if ok else 'ПРОБЛЕМА ✗'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
