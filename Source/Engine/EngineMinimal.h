/**
 * @file EngineMinimal.h
 * @brief 엔진의 최소 공통 헤더입니다(CoreMinimal + OS 헤더 + EngineDefines + ResourceUtil).
 * @note 그래픽 · 미디어 API 헤더(`Engine/Common/EnginePlatformHeaders.h` — D3D11/12 · DXGI · D3DCompiler · Media Foundation ·
 *       XAudio2)는 넣지 않습니다. 이 헤더는 PCH 를 거쳐 엔진의 모든 TU 에 들어가므로, 넣으면 그 헤더들을 쓰지 않는 TU 1,600 여 개가
 *       `d3d12.h` 를 파싱합니다. 쓰는 파일이 `EnginePlatformHeaders.h` 를 직접 include 합니다.
 */
#pragma once
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/CoreMinimal.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/ResourceUtil.h"
