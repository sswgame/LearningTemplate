/**
 * @file EngineMinimal.h
 * @brief 엔진의 최소 공통 헤더입니다(CoreMinimal + EngineDefines + ResourcePaths).
 * @note 그래픽 · 미디어 API 헤더(`Engine/Common/EnginePlatformHeaders.h` — D3D11/12 · DXGI · D3DCompiler · Media Foundation ·
 *       XAudio2)는 넣지 않습니다. 이 헤더는 PCH 를 거쳐 엔진의 모든 TU 에 들어가므로, 넣으면 그 헤더들을 쓰지 않는 TU 1,600 여 개가
 *       `d3d12.h` 를 파싱합니다. 쓰는 파일이 `EnginePlatformHeaders.h` 를 직접 include 합니다.
 */
#pragma once
// Windows 의 OS 헤더(`Windows.h` · `DbgHelp.h` …)는 넣지 않는다 — 이 헤더는 PCH 를 거쳐 모든 TU 에 들어가 TU 마다 파싱 비용이 든다.
// Win32 · POSIX API 를 쓰는 `.cpp` 가 `Core/Common/PlatformOsHeaders.h` 를 직접 include 한다.
#include "Core/CoreMinimal.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"
