/**
 * @file SplashSubsystem.h
 * @brief 기동 초기(에셋 시스템이 선 직후)에 스플래시 창을 띄우는 기동 단계입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Window/SplashWindow.h"

namespace sw
{
    /**
     * @class SplashSubsystem
     * @brief 스플래시 창(`SplashWindow`)을 기동 초기에 띄웁니다. 진행 표시와 닫기는 호스트(App)가 기동 끝에 한다(`getSplash`).
     * @details 스플래시 이미지는 에셋(`textures/splash.dds`)이라 에셋 시스템 뒤에 선다 — 예전에는 App 이 엔진 초기화(RHI 디바이스 생성 포함, 수 초)를 다 기다린 뒤에
     *          띄워 그동안 화면에 아무것도 없었다. 순서는 의존 · 이름 순이라 에셋 직후 · 오디오 · 모듈 · RHI 앞에서 돈다(언리얼은 실행 직후 스플래시를 띄운다).
     *          무인 실행(-unattended)은 띄우지 않는다(보는 사람이 없다). 호스트가 닫지 못하고 기동이 실패하면 종료가 닫는다.
     */
    class SW_API SplashSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "Splash";

        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override { _splash.dismiss(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;

        /** @brief 스플래시 창입니다. 띄우지 않았으면 갱신 · 닫기가 아무것도 하지 않는다(`SplashWindow`). */
        SplashWindow& getSplash() { return _splash; }

    private:
        SplashWindow _splash;
    };
} // namespace sw
