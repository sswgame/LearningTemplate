#!/usr/bin/env python3
"""
@file CompareWindowToCapture.py
@brief 화면에 실제로 나온 창과 `-gv_screenshot` 캡처의 방향(그대로 · 상하 반전 · 좌우 반전)을 백엔드마다 견준다.

`-gv_screenshot` 은 오프스크린 텍스처를 읽는다. 그래서 창으로 옮기는 단계에만 생긴 반전은 캡처로는 보이지 않는다
(GL 은 창의 0 행이 아래라 그 단계에서 뒤집히기 쉽다). 이 도구는 창의 클라이언트 영역을 화면에서 직접 잡아 캡처와 견준다.

사용법 (빌드 후, Windows):
    py -3 Scripts/dev/CompareWindowToCapture.py                         # 네 백엔드, 창 ↔ 캡처
    py -3 Scripts/dev/CompareWindowToCapture.py --backends gl --repeat 3
    py -3 Scripts/dev/CompareWindowToCapture.py --windows-only          # 캡처 없이 창끼리(첫 백엔드가 기준)

판정: 차이(채널 평균 절대차)가 가장 작은 방향을 고른다. 정상이면 `same` 이 0 에 가깝다. 창을 잡는 동안 다른 창이 앞을 가리면
값이 커지므로(MISMATCH) 그때는 다시 돌린다. Pillow 가 필요하다.
"""
import argparse
import ctypes
import ctypes.wintypes as wintypes
import os
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageGrab, ImageStat

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import getProjectRoot  # noqa: E402

kMinClientArea = 320 * 240
kMismatchThreshold = 4.0


def getClientRectInternal(user32, hwnd):
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    origin = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return (origin.x, origin.y, origin.x + rect.right, origin.y + rect.bottom)


def findMainWindowInternal(user32, pid):
    """그 프로세스의 보이는 창 가운데 클라이언트 영역이 가장 큰 것(스플래시가 아니라 본 창)입니다."""
    listWindow = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def onWindow(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd) and user32.GetWindowTextLengthW(hwnd) > 0:
            listWindow.append(hwnd)
        return True

    user32.EnumWindows(onWindow, 0)
    best, bestArea = None, 0
    for hwnd in listWindow:
        left, top, right, bottom = getClientRectInternal(user32, hwnd)
        area = (right - left) * (bottom - top)
        if area > bestArea:
            best, bestArea = hwnd, area
    return best if bestArea >= kMinClientArea else None


def computeDifferenceInternal(imageA, imageB):
    return sum(ImageStat.Stat(ImageChops.difference(imageA, imageB)).mean) / 3.0


def grabWindowInternal(binDir, backend, screenshotPath, extraArgs):
    """App 을 띄워 창이 서면 잡는다. 캡처를 켰으면 끝날 때까지 기다린다. (창 이미지, 종료 코드)를 돌려준다."""
    user32 = ctypes.windll.user32
    listArg = [os.path.join(binDir, "App.exe"), "-" + backend, "-gv_profileFrames=20000"]
    if screenshotPath:
        listArg += ["-gv_screenshotFrame=200", "-gv_screenshot=" + screenshotPath]
    process = subprocess.Popen(listArg + extraArgs, cwd=binDir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    hwnd = None
    for _ in range(300):
        hwnd = findMainWindowInternal(user32, process.pid)
        if hwnd:
            break
        time.sleep(0.1)
    # 먼저 뜨는 스플래시도 크기 문턱을 넘는다. 잠시 뒤 다시 찾아 본 창을 잡는다.
    time.sleep(1.5)
    hwnd = findMainWindowInternal(user32, process.pid) or hwnd
    if hwnd is None:
        process.kill()
        return None, -1
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    time.sleep(3.0)
    rect = getClientRectInternal(user32, hwnd)
    if rect[2] - rect[0] < 64 or rect[3] - rect[1] < 64:
        process.kill()
        return None, -1
    image = ImageGrab.grab(bbox=rect, all_screens=True).convert("RGB")
    if screenshotPath:
        return image, process.wait(timeout=600)
    process.kill()
    return image, 0


def judgeOrientationInternal(window, reference):
    reference = reference.resize(window.size)
    same = computeDifferenceInternal(window, reference)
    vflip = computeDifferenceInternal(window, reference.transpose(Image.FLIP_TOP_BOTTOM))
    hflip = computeDifferenceInternal(window, reference.transpose(Image.FLIP_LEFT_RIGHT))
    best = min(same, vflip, hflip)
    if best > kMismatchThreshold:
        verdict = "MISMATCH"
    elif best == same:
        verdict = "OK"
    else:
        verdict = "UPSIDE-DOWN" if best == vflip else "MIRRORED"
    return same, vflip, hflip, verdict


def main():
    parser = argparse.ArgumentParser(description="실제 창과 캡처의 방향 비교")
    parser.add_argument("--preset", default="Ninja-Debug")
    parser.add_argument("--backends", default="dx12,dx11,vk,gl")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--windows-only", action="store_true", help="캡처 없이 창끼리 견준다(첫 백엔드가 기준)")
    parser.add_argument("--out", default=None, help="잡은 이미지 폴더 (기본: build/<preset>/windowcapture)")
    parser.add_argument("extra", nargs="*", help="App 에 넘길 인자")
    args = parser.parse_args()

    if sys.platform != "win32":
        print("Windows 전용입니다.")
        return 2
    ctypes.windll.shcore.SetProcessDpiAwareness(2)

    repo = str(getProjectRoot())
    binDir = os.path.join(repo, "build", args.preset, "Bin")
    outDir = args.out or os.path.join(repo, "build", args.preset, "windowcapture")
    os.makedirs(outDir, exist_ok=True)

    failed = False
    reference = None
    for run in range(args.repeat):
        for backend in args.backends.split(","):
            screenshotPath = None if args.windows_only else os.path.join(outDir, "capture_%s.ppm" % backend)
            if screenshotPath and os.path.exists(screenshotPath):
                os.remove(screenshotPath)
            window, exitCode = grabWindowInternal(binDir, backend, screenshotPath, args.extra)
            if window is None:
                print("%-5s 창을 잡지 못했습니다" % backend)
                failed = True
                continue
            window.save(os.path.join(outDir, "window_%s_%d.png" % (backend, run)))
            if args.windows_only:
                if reference is None:
                    reference = window
                    print("%-5s 기준" % backend)
                    continue
                compareTo = reference
            else:
                if os.path.exists(screenshotPath) is False:
                    print("%-5s 캡처가 없습니다 (exit %d)" % (backend, exitCode))
                    failed = True
                    continue
                compareTo = Image.open(screenshotPath).convert("RGB")
            same, vflip, hflip, verdict = judgeOrientationInternal(window, compareTo)
            print("%-5s exit=%d  same=%.1f vflip=%.1f hflip=%.1f -> %s" % (backend, exitCode, same, vflip, hflip, verdict))
            failed = failed or verdict != "OK"
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
