/**
 * @file GPUDiagnosticsBuild.cpp
 * @brief 개발 구성 — GPU 진단 장치를 켤 수 있다. 배포는 `GPUDiagnosticsBuildShipping.cpp` 를 CMake 가 고른다(`Source/Engine/CMakeLists.txt`).
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/GPUDiagnosticsBuild.h"

namespace sw
{
    bool GPUDiagnosticsBuild::isAvailable()
    {
        return true;
    }
} // namespace sw
