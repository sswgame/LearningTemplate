/**
 * @file Engine/CoreServices/LoggerSubsystem.h
 * @brief 기본 로그 싱크(`AsyncLogSink`)를 세웁니다. 코어 범위에서 이름 풀 · 명령줄 다음 — 이 뒤의 모든 서브시스템은 로그를 남길 수 있다.
 * @details 로거가 범위의 맨 앞(진단 도구 · 이름 풀 다음)에 서므로 초기화 · 종료의 로그는 모두 남는다. 다만 **정렬 오류**(선언 오타 · 순환)는 어느 초기화보다
 *          먼저 나므로 로거가 없다 — 부른 쪽(호스트)이 표준 오류로 낸다.
 *          명령줄이 로거보다 먼저 서는 까닭은 로그 파일(`-log-file`)이 명령줄로 정해지기 때문이다. 그 사이에 남은 줄(모르는 인자 경고 등)은 `Logger` 가 쌓아 두었다가
 *          싱크가 등록될 때 넘긴다(언리얼 FOutputDeviceRedirector 의 backlog). 로거를 서브시스템으로 둔 것은 그 구간을 좁힐 뿐 없애지는 않는다.
 *          종료에서 로거 **스레드**를 세우고(그 스레드도 메모리를 풀며 메모리 프로파일러를 부른다), 객체는 소멸자에서 놓는다 — 그 사이의 로그는 출력에 바로 쓰인다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class AsyncLogSink;
    class CommandLineSubsystem;

    /** @class LoggerSubsystem @brief 로거 서브시스템입니다. */
    class SW_API LoggerSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Logger";

        /** @param commandLine 로거보다 먼저 선다 — 해석된 `-log-file` 로 파일 장치를 연다. */
        explicit LoggerSubsystem( const CommandLineSubsystem& commandLine );
        ~LoggerSubsystem() override;

        LoggerSubsystem( const LoggerSubsystem& )            = delete;
        LoggerSubsystem& operator=( const LoggerSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        /** @brief 로거 스레드를 세웁니다(객체는 소멸자까지 남는다). */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

    private:
        const CommandLineSubsystem& _commandLine;
        unique_ptr<AsyncLogSink>    _logger; ///< 처음 서고 범위의 끝에 사라진다
    };
} // namespace sw
