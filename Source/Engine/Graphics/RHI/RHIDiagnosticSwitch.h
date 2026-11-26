/**
 * @file RHIDiagnosticSwitch.h
 * @brief 장치를 만들 때 켜는 GPU 진단(검증 층 · DRED · GPU 브레드크럼)을 켤지 정하는 함수입니다 — EngineConfig `_rhi` 의 기본값과 시작 인자를 합친다.
 * @details 장치를 만들 때(`IRHIDevice::initialize`) 진단마다 한 번만 부른다. 실행 중에 바꾸는 개발 변수가 아니다 — 이 진단들은 장치를 만들기 전에만 걸 수 있다.
 */
#pragma once
#include "Core/CommandLine/CommandLine.h"
#include "Core/Foundation/BuildConfig.h"
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

    /** @brief 이 바이너리의 구성입니다. 장치 생성이 진단을 풀 때 쓴다(구성마다 고르는 식을 이 한 곳에 둔다). */
    inline constexpr RHIDiagnosticBuild kCurrentRHIDiagnosticBuild = build::kShipping ? RHIDiagnosticBuild::Shipping
                                                                   : build::kDebug    ? RHIDiagnosticBuild::Debug
                                                                                      : RHIDiagnosticBuild::Release;

    /** @brief 장치를 만들 때 켤 GPU 진단 셋입니다(`resolveRHIDiagnosticSwitches`). */
    struct RHIDiagnosticSwitches
    {
        bool _bValidationLayer{ false }; ///< 그래픽 API 검증 층. 배포 구성은 검증 층을 짓지 않아 늘 false 다
        bool _bDRED{ false };            ///< D3D12 DRED. 배포 구성에서도 듣는다
        bool _bGPUBreadcrumbs{ false };  ///< 패스 GPU 표지. 배포 구성에서도 듣는다
    };

    /**
     * @brief 진단 하나를 켤지 정합니다. 명령줄에 @p argument(`-rhi-validation` · `-rhi-dred` · `-rhi-breadcrumbs`, `=0` 은 끔)가 있으면 그것이 이기고, 없으면 @p configValue 를 따른다.
     * @param configValue EngineConfig `_rhi` 의 그 진단 칸
     * @param argument 그 진단의 시작 인자(flag 인자)
     * @param pCommandLine 명령줄 표 — 엔진 서비스가 없는 실행(nullptr)은 설정값만 본다
     * @param build 지금 구성
     * @return 켤지. @p configValue 가 `None`(파일에 값이 없다)이거나 모르는 값이면 오류다(대상은 인자 철자) — 기본값을 짐작하지 않는다.
     */
    [[nodiscard]] SW_API expected<bool> resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch configValue, CommandLineArgument argument, const CommandLine* pCommandLine,
                                                                    RHIDiagnosticBuild build );

    /**
     * @brief EngineConfig `_rhi` 의 진단 칸 셋(`_validationLayer` · `_dred` · `_gpuBreadcrumbs`)을 한 번에 풉니다. 칸마다 `resolveRHIDiagnosticSwitch` 의 규칙이다.
     * @param build 지금 구성. 장치는 `kCurrentRHIDiagnosticBuild` 를 넘기고 테스트는 다른 구성을 넘긴다
     * @return 값이 없는 칸이 있으면 그 칸의 오류(대상은 인자 철자)
     */
    [[nodiscard]] SW_API expected<RHIDiagnosticSwitches> resolveRHIDiagnosticSwitches( const RHIConfig& config, const CommandLine* pCommandLine, RHIDiagnosticBuild build );
} // namespace sw
