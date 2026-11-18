/**
 * @file RHIDiagnosticSwitch.h
 * @brief 장치를 만들 때 켜는 GPU 진단(검증 층 · DRED · GPU 브레드크럼)을 켤지 정하는 함수입니다 — EngineConfig `_rhi` 의 기본값과 시작 인자를 합친다.
 * @details 장치를 만들 때(`IRHIDevice::initialize`) 진단마다 한 번만 부른다. 실행 중에 바꾸는 개발 변수가 아니다 — 이 진단들은 장치를 만들기 전에만 걸 수 있다.
 */
#pragma once
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    /** @brief `RHIDiagnosticSwitch` 의 구성별 값(`DebugBuildOnly` · `NonShippingOnly`)이 보는 지금 구성입니다. */
    enum class RHIDiagnosticBuild : uint8
    {
        Debug,
        Release,
        Shipping,
    };

    /**
     * @brief 진단 하나를 켤지 정합니다. 명령줄에 @p argument(`-rhi-validation` · `-rhi-dred` · `-rhi-breadcrumbs`, `=0` 은 끔)가 있으면 그것이 이기고, 없으면 @p configValue 를 따른다.
     * @param configValue EngineConfig `_rhi` 의 그 진단 칸
     * @param argument 그 진단의 시작 인자(flag 인자)
     * @param pCommandLine 명령줄 표 — 엔진 서비스가 없는 실행(nullptr)은 설정값만 본다
     * @param build 지금 구성
     * @return 켤지. @p configValue 가 `None`(파일에 값이 없다)이거나 모르는 값이면 오류다(대상은 인자 철자) — 기본값을 짐작하지 않는다.
     */
    [[nodiscard]] SW_API expected<bool> resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch configValue, CommandLineArgument argument, const CommandLineManager* pCommandLine,
                                                                    RHIDiagnosticBuild build );
} // namespace sw
