/**
 * @file Engine/Bootstrap/CrashHandlerSubsystem.h
 * @brief 크래시 핸들러를 설치하고 크래시 리포트에 함께 나갈 빌드 값(빌드 · 플랫폼 · 빌드 id)을 적습니다. 명령줄이 크래시 보고 프로세스인지도 판정합니다.
 * @details 로거 다음에 선다 — 이후 어디서 죽든 콜 스택이 남는다. 크래시 보고 프로세스(`-crash-reporter=<폴더>`)면 핸들러를 세우지 않고 그 사실만 적는다 —
 *          핸들러를 세우면 그 프로세스가 죽을 때 보고 프로세스를 또 띄운다(재귀). 호스트는 이 범위가 선 뒤 `isCrashReporterRun` 을 보고 엔진을 세우지 않는다.
 *          명령줄이 없는 실행 파일(도구 — 자기 인자를 스스로 읽는다)은 판정 없이 설치만 한다. 핸들러는 소멸자에서 뗀다(범위의 마지막 해제까지 콜 스택을 남긴다).
 *          리소스 루트 · 게임 선택은 엔진을 세우는 호스트의 일이라 `ContentRootSubsystem` 이 따로 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class CommandLineSubsystem;

    /** @class CrashHandlerSubsystem @brief 크래시 핸들러 서브시스템입니다. */
    class SW_API CrashHandlerSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "CrashHandler";

        /** @brief @p pCommandLine 은 같은 범위의 것이다(이것보다 먼저 선다). nullptr 이면 크래시 보고 프로세스를 판정하지 않는다(도구). */
        explicit CrashHandlerSubsystem( const CommandLineSubsystem* pCommandLine );
        /** @brief 설치했으면 핸들러를 뗍니다. */
        ~CrashHandlerSubsystem() override;

        CrashHandlerSubsystem( const CrashHandlerSubsystem& )            = delete;
        CrashHandlerSubsystem& operator=( const CrashHandlerSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        /** @brief 명령줄이 있으면 명령줄 뒤, 없으면 로거 뒤에 선다. */
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 명령줄이 크래시 보고 프로세스(`-crash-reporter=<폴더>`)였으면 true 입니다 — 핸들러를 세우지 않았다. */
        bool isCrashReporterRun() const { return _bCrashReporterRun; }

    private:
        const CommandLineSubsystem* _pCommandLine;
        bool                        _bInstalled;        ///< 핸들러를 설치했다(= 소멸자가 뗄 것이 있다)
        bool                        _bCrashReporterRun; ///< 명령줄이 크래시 보고 프로세스였다
    };
} // namespace sw
