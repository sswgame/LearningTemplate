#include "pch.h"

#include "Engine/EngineBootstrap.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineOwnedServices.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "EngineBootstrap" );

    EngineBootstrap::EngineBootstrap()
        : _pOwned{ nullptr }
        , _logger{ nullptr }
        , _deadlockDetector{ nullptr }
        , _memoryProfiler{ nullptr }
        , _bStarted{ false }
    {
    }

    EngineBootstrap::~EngineBootstrap()
    {
        shutdown();
    }

    bool EngineBootstrap::initialize( EngineOwnedServices& owned, bool bDiagnostics )
    {
        // 로거 · 명령줄 · 전역 변수 표다. 프로파일러가 아래에서 서므로 그 앞의 할당은 세이지 않는다.
        SW_MEMORY_SCOPE( EngineMisc );
        _pOwned   = &owned;
        _bStarted = true;
        HashedStringPool::initialize();

        _logger = make_unique<Logger>();
        _logger->initialize();
        // 로거 직후에 설치해야 이후 어디서 죽든 콜 스택이 남는다.
        CrashHandler::initialize();

        // 리소스 루트는 **로거 · 크래시 핸들러 다음**에 찾는다. 루트를 찾지 못했을 때의 진단(`RootFolder` 로그와 assert 메시지)이
        // 로거에 남아야 한다. `initialize()` 는 once_flag 라 두 번째 호출은 아무것도 다시 찍지 않으므로 **첫 호출이 로거 뒤에 와야** 한다.
        if ( ResourceUtil::initialize() == false )
        {
            SW_LOG_ERROR( "리소스 루트를 찾지 못했습니다 — Resource/ 가 있는 위치에서 실행하십시오." );
            return false;
        }

        // 크래시 리포트에 함께 나갈 값들이다. 덤프만으로는 알 수 없는 것들이다. 백엔드는 RHI 단계가 덮어쓴다.
        CrashHandler::setContextValue( "Build", build::kConfigName );
        CrashHandler::setContextValue( "Platform", build::kPlatformName );

        if ( bDiagnostics )
        {
            _deadlockDetector = make_unique<DeadlockDetector>();
            _deadlockDetector->initialize();
            _memoryProfiler = make_unique<MemoryProfiler>();
            _memoryProfiler->initialize();
        }

        // 명령줄 · 전역 변수는 표의 서비스이지만 파싱이 다른 서비스 생성보다 먼저라 여기서 만든다(`createAll` 은 있는 것을 덮지 않는다).
        if ( owned._pCommandLineManager == nullptr )
            owned._pCommandLineManager = make_unique<CommandLineManager>();
        owned._pCommandLineManager->initialize();
        if ( owned._pGlobalVariableManager == nullptr )
            owned._pGlobalVariableManager = make_unique<GlobalVariableManager>();
        owned._pGlobalVariableManager->registerPendingVariables( "Engine", GlobalVariableRegistrar::getHead() );
        GlobalVariableRegistrar::getHead() = nullptr;
        owned._pGlobalVariableManager->registerToCommandLine( owned._pCommandLineManager.get() );
        return true;
    }

    void EngineBootstrap::parseCommandLine( int32 argc, utf8* pArgv[] )
    {
        _pOwned->_pCommandLineManager->parse( argc, pArgv );
        _pOwned->_pGlobalVariableManager->updateFromCommandLine( _pOwned->_pCommandLineManager.get() );
    }

    void EngineBootstrap::fillServices( EngineServices& outServices ) const
    {
        _pOwned->bindInto( outServices );
        outServices._pMemoryProfiler = _memoryProfiler.get();
    }

    void EngineBootstrap::shutdown()
    {
        if ( _bStarted == false )
            return;
        _bStarted = false;

        if ( _pOwned != nullptr )
        {
            // 변수를 기본값으로 되돌린다. 값 변경 콜백을 부르므로 콜백을 건 쪽(App 의 백엔드 교체)은 그 전에 떼어 두었다.
            if ( _pOwned->_pGlobalVariableManager != nullptr )
                _pOwned->_pGlobalVariableManager->shutdown();
            // 단계에 속하지 않는 서비스(이벤트 · 지역화 · 디버그 도구 · 프로파일러 · 백엔드 등록부 · 전역 변수 · 명령줄)를 목록의 역순으로 놓는다.
            _pOwned->destroyAll();
        }
        // 표가 가리키던 것이 모두 사라졌다. 이 뒤로 `engine::get*` 은 쓰지 않는다.
        engine::unbindEngineServices();

        // 로거 **스레드**는 메모리 프로파일러보다 먼저 세운다. 그 스레드도 메모리를 풀며 프로파일러를 부른다(`Memory::free` → `recordFree`).
        // 로거 객체는 맨 마지막에 놓는다 — 그 사이의 로그는 출력에 바로 쓰인다.
        if ( _logger != nullptr )
            _logger->shutdown();
        if ( _memoryProfiler != nullptr )
            _memoryProfiler->shutdown();
        _memoryProfiler.reset();
        if ( _deadlockDetector != nullptr )
            _deadlockDetector->shutdown();
        _deadlockDetector.reset();

        // 이름 풀은 hashed_string 을 든 객체가 모두 사라진 뒤에 내린다.
        HashedStringPool::shutdown();
        CrashHandler::shutdown();
        _logger.reset();
    }
} // namespace sw
