/**
 * @file Engine/Bootstrap/CrashHandlerSubsystem.h
 * @brief 크래시 핸들러를 설치하고 리소스 루트 · 돌릴 게임을 정합니다. 크래시 리포트에 함께 나갈 값(게임 · 빌드 · 플랫폼 · 빌드 id)도 적습니다.
 * @details 로거 · 명령줄 다음에 선다 — 이후 어디서 죽든 콜 스택이 남고, 루트를 못 찾았을 때의 진단이 로거에 남는다(`ResourcePaths::initialize` 는
 *          once_flag 라 첫 호출이 로거 뒤에 와야 한다). 크래시 보고 프로세스(`-crash-reporter=<폴더>`)면 아무것도 세우지 않고 그 사실만 적는다 —
 *          핸들러를 세우면 그 프로세스가 죽을 때 보고 프로세스를 또 띄우고(재귀), 리소스 루트는 쓰지 않으므로 못 찾아도 서야 한다. 호스트는 이 범위가 선 뒤
 *          `isCrashReporterRun` 을 보고 엔진을 세우지 않는다. 핸들러는 소멸자에서 뗀다(범위의 마지막 해제까지 콜 스택을 남긴다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class CommandLineSubsystem;

    /** @class CrashHandlerSubsystem @brief 크래시 핸들러 · 리소스 루트 · 게임 선택 서브시스템입니다. */
    class SW_API CrashHandlerSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "CrashHandler";

        /** @brief @p commandLine 은 같은 범위의 것이다(이것보다 먼저 서고 나중에 사라진다). */
        explicit CrashHandlerSubsystem( const CommandLineSubsystem& commandLine );
        /** @brief 설치했으면 핸들러를 뗍니다. */
        ~CrashHandlerSubsystem() override;

        CrashHandlerSubsystem( const CrashHandlerSubsystem& )            = delete;
        CrashHandlerSubsystem& operator=( const CrashHandlerSubsystem& ) = delete;

        /** @brief 리소스 루트를 못 찾거나 이 빌드에 없는 게임이면 `Failed` 입니다(진단은 로거에 남는다). */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 명령줄이 크래시 보고 프로세스였으면 true 입니다 — 핸들러 · 리소스 루트를 세우지 않았다. */
        bool isCrashReporterRun() const { return _bCrashReporterRun; }

    private:
        const CommandLineSubsystem& _commandLine;
        bool                        _bInstalled;        ///< 핸들러를 설치했다(= 소멸자가 뗄 것이 있다)
        bool                        _bCrashReporterRun; ///< 명령줄이 크래시 보고 프로세스였다
    };
} // namespace sw
