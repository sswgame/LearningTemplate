/**
 * @file pch.h
 * @brief 엔진 공통 Precompiled Header (CoreMinimal + EngineMinimal)
 * @details CMake `sw_configurePch`가 `SW_ENABLE_PCH=ON`일 때 타겟별로 이 헤더를 강제 포함합니다.
 *
 * @note **자주 바뀌는 헤더를 넣지 않습니다.** 여기 든 헤더 하나가 바뀌면 이 PCH를 쓰는 TU 약 2,200개가 전부 다시 컴파일됩니다.
 *       `Component.h` · `GameObject.h` · `Scene.h` · `RHITypes.h` 는 쓰는 .cpp 가 직접 include 합니다(바뀌었을 때 다시 컴파일되는 TU:
 *       3,094 → 160 ~ 850). 대신 풀 빌드가 약 15 % 느립니다.
 */
#pragma once
#include "Core/CoreMinimal.h"

#include "Engine/Common/Common.h"
#include "Engine/EngineMinimal.h"
