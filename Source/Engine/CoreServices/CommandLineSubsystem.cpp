#include "pch.h"

#include "Engine/CoreServices/CommandLineSubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
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
        // 경로 인자의 상대 경로 기준은 기동 때 이 프로세스가 받은 작업 폴더다(그 셸에서 적은 상대 경로의 뜻) - 여기서 한 번 읽고 다시 읽지 않는다.
        const std::optional<AbsolutePath> baseDirectory = AbsolutePath::tryMake( FileUtil::getCurrentPath() );
        if ( baseDirectory.has_value() == false )
        {
            SW_LOW_LEVEL_ERROR( "Cannot read the startup directory - command-line paths cannot be resolved" ); // 로거는 명령줄 다음에 선다 - stderr 로 바로 쓴다
            return SubsystemInitResult::Failed;
        }
        GlobalVariableManager::setPathBaseDirectory( *baseDirectory ); // 경로 전역 변수(콘솔 · 에디터 · 보류 값)도 같은 기준으로 푼다
        _commandLine->parse( _argc, _ppArgv, *baseDirectory );
        // 인자는 부른 쪽의 것이다(테스트 하네스는 지역 목록을 넘긴다) — 초기화 뒤로는 들지 않는다.
        _argc   = 0;
        _ppArgv = nullptr;
        if ( _commandLine->getParseErrors().empty() == false )
        {
            // 틀린 인자로 다른 일을 하지 않는다(값을 0 · false 로 바꿔 계속하던 폴백을 없앴다).
            for ( const string& error : _commandLine->getParseErrors() )
            {
                SW_LOW_LEVEL_ERROR( "Invalid command-line argument: %s", error.c_str() ); // 로거는 명령줄 다음에 선다 - stderr 로 바로 쓴다
            }
            return SubsystemInitResult::Failed;
        }
        _globalVariables->updateFromCommandLine( _commandLine.get() );
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
