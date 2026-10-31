/**
 * @file GPUDiagnosticsBuildShipping.cpp
 * @brief 배포 구성 — GPU 진단 장치를 켜지 않는다(검증 비용 · 디버그 컨텍스트 · 진단 버퍼를 만들지 않는다). 개발은 `GPUDiagnosticsBuild.cpp`.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/GPUDiagnosticsBuild.h"

namespace sw
{
    bool GPUDiagnosticsBuild::isAvailable()
    {
        return false;
    }
} // namespace sw
