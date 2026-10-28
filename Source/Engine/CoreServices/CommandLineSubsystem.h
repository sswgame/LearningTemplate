/**
 * @file Engine/CoreServices/CommandLineSubsystem.h
 * @brief 명령줄과 전역 변수 표를 세웁니다 — 엔진 전역 변수를 등록하고, 명령줄 인자로 등록한 뒤 파싱해 값을 넣습니다.
 * @details 둘은 한 서브시스템이다. 전역 변수가 명령줄 인자로 먼저 등록돼야 파싱이 그 값을 바로 넣고(enum 이름은 리플렉션 뒤로 미룬다), 명령줄을 읽는 쪽은
 *          모두 그 결과를 본다 — 둘로 나누면 "초기화됐지만 아직 파싱하지 않은" 명령줄이 생긴다. 인자는 만들 때 받고 초기화 동안만 읽는다(복사하지 않는다).
 *          종료는 전역 변수를 기본값으로 되돌린다(값 변경 콜백을 건 쪽 — App 의 백엔드 교체 — 은 그 전에 뗐다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class CommandLineManager;
    class GlobalVariableManager;

    /** @class CommandLineSubsystem @brief 명령줄 · 전역 변수 서브시스템입니다. */
    class SW_API CommandLineSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "CommandLine";

        /** @brief 파싱할 인자입니다. @p pArgv 는 초기화가 끝날 때까지 살아 있어야 합니다. */
        CommandLineSubsystem( int32 argc, utf8* pArgv[] );
        ~CommandLineSubsystem() override;

        CommandLineSubsystem( const CommandLineSubsystem& )            = delete;
        CommandLineSubsystem& operator=( const CommandLineSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        /** @brief 전역 변수를 기본값으로 되돌립니다. */
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 명령줄입니다(빈 표로 만들어 두고 초기화가 채운다 — 호스트가 범위에 `bind` 한다). */
        CommandLineManager* findCommandLine() const { return _commandLine.get(); }
        /** @brief 전역 변수 표입니다(만들 때 생기고 초기화가 채운다). */
        GlobalVariableManager* findGlobalVariables() const { return _globalVariables.get(); }

    private:
        unique_ptr<CommandLineManager>    _commandLine;
        unique_ptr<GlobalVariableManager> _globalVariables;
        int32                             _argc;   ///< 만들 때 받은 인자 수(초기화 동안만 읽는다)
        utf8**                            _ppArgv; ///< 만들 때 받은 인자(초기화 동안만 읽는다)
    };
} // namespace sw
