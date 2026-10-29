#include "pch.h"

#include "Engine/CoreServices/CommandLineSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/CoreServices/NamePoolSubsystem.h"

namespace sw
{
    CommandLineSubsystem::CommandLineSubsystem( int32 argc, utf8* pArgv[] )
        : _commandLine{ make_unique<CommandLineManager>() }
        , _globalVariables{ make_unique<GlobalVariableManager>() }
        , _argc{ argc }
        , _ppArgv{ pArgv }
    {
    }

    CommandLineSubsystem::~CommandLineSubsystem()
    {
        // 전역 변수 표가 명령줄을 가리킨다 — 표를 먼저 놓는다.
        _globalVariables.reset();
        _commandLine.reset();
    }

    SubsystemInitResult CommandLineSubsystem::initializeSubsystem()
    {
        _commandLine->initialize();
        _globalVariables->registerPendingVariables( "Engine", GlobalVariableRegistrar::getHead() );
        GlobalVariableRegistrar::getHead() = nullptr;
        _globalVariables->registerToCommandLine( _commandLine.get() );
        _commandLine->parse( _argc, _ppArgv );
        _globalVariables->updateFromCommandLine( _commandLine.get() );
        // 인자는 부른 쪽의 것이다(테스트 하네스는 지역 목록을 넘긴다) — 초기화 뒤로는 들지 않는다.
        _argc   = 0;
        _ppArgv = nullptr;
        return SubsystemInitResult::Succeeded;
    }

    void CommandLineSubsystem::shutdownSubsystem()
    {
        // 변수를 기본값으로 되돌린다. 값 변경 콜백을 부르므로 콜백을 건 쪽(App 의 백엔드 교체)은 그 전에 떼어 두었다.
        _globalVariables->shutdown();
    }

    span<const utf8* const> CommandLineSubsystem::getSubsystemDependencies() const
    {
        // 로거보다 먼저 선다 — 로그 파일(`-log-file`)이 명령줄로 정해진다. 그 사이의 경고(모르는 인자 · 틀린 값)는 Logger 가 쌓았다가 싱크에 넘긴다.
        // 로그 인자 · 전역 변수 이름에 이름(hashed_string)이 쓰인다 — 이름 풀이 먼저 선다.
        static constexpr const utf8* kArrDependency[] = { NamePoolSubsystem::kSubsystemName };
        return kArrDependency;
    }
} // namespace sw
