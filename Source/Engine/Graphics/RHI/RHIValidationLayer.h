/**
 * @file RHIValidationLayer.h
 * @brief 그래픽 API 검증 층을 켤지 정하는 함수입니다 — EngineConfig `_rhi._validationLayer` 와 시작 인자 `-rhi-validation` 을 합친다.
 * @details 장치를 만들 때(`IRHIDevice::initialize`) 한 번만 부른다. 실행 중에 바꾸는 개발 변수가 아니다 — 검증 층은 장치를 만들기 전에만 걸 수 있다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    class CommandLineManager;

    /**
     * @brief 검증 층을 켤지 정합니다. 명령줄에 `-rhi-validation`(`=0`)이 있으면 그것이 이기고, 없으면 @p configMode 를 따른다.
     * @param configMode EngineConfig `_rhi._validationLayer`
     * @param pCommandLine 명령줄 표 — 엔진 서비스가 없는 실행(nullptr)은 설정값만 본다
     * @param bDebugBuild Debug 구성인지(`RHIValidationLayerMode::DebugBuildOnly` 가 본다)
     * @return 켤지. @p configMode 가 `None`(파일에 값이 없다)이거나 모르는 값이면 오류다 — 기본값을 짐작하지 않는다.
     */
    [[nodiscard]] SW_API expected<bool> resolveRHIValidationLayer( RHIValidationLayerMode configMode, const CommandLineManager* pCommandLine, bool bDebugBuild );
} // namespace sw
