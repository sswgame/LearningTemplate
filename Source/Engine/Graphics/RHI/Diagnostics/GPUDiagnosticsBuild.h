/**
 * @file GPUDiagnosticsBuild.h
 * @brief 이 빌드가 GPU 진단 장치(브레드크럼 · DRED · 장치 결함 확장 · GPU 단언 · NaN 검사 · bindless 검사 · GL 디버그 컨텍스트)를 켤 수 있는지 하나입니다.
 * @details 구현은 CMake 가 고른다 — 개발은 `GPUDiagnosticsBuild.cpp`(true), 배포는 `GPUDiagnosticsBuildShipping.cpp`(false). 장치마다 `#if SW_SHIPPING` 을 두지 않아
 *          진단 코드는 모든 구성에서 컴파일되고(배포에서만 깨지는 일이 없다) 켜는지만 여기서 정한다. 언리얼은 같은 장치(DRED · GPU 브레드크럼)를 `!UE_BUILD_SHIPPING` 에서만 켠다.
 *          장치마다의 스위치(`-gv_gpuBreadcrumbs` …)는 이 값과 함께 본다 — 배포는 스위치를 등록하지 않아 기본값이 남지만, 기본값이 켬인 장치도 여기서 막힌다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    /**
     * @struct GPUDiagnosticsBuild
     * @brief 이 빌드가 GPU 진단 장치를 켤 수 있는지입니다(빌드마다 고정 — 인터페이스 없이 구현 파일을 CMake 가 고른다).
     */
    struct SW_API GPUDiagnosticsBuild
    {
        /** @brief 진단 장치를 켤 수 있는 빌드(개발)면 true, 배포면 false 입니다. */
        static bool isAvailable();
    };
} // namespace sw
