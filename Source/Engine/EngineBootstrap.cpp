#include "pch.h"

#include "Engine/EngineBootstrap.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/DeadlockDetector.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/Process/ModuleBuildId.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineServiceCollection.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
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

    bool EngineBootstrap::initialize( EngineServiceCollection& owned, bool bDiagnostics )
    {
        // 이름 풀 · 로거 · 명령줄 · 전역 변수 표다.
        SW_MEMORY_SCOPE( EngineMisc );
        _pOwned   = &owned;
        _bStarted = true;

        // 메모리 프로파일러가 맨 먼저 선다. 그 앞에서 잡은 sw 블록(로거의 큐 · 이름 풀)은 태그에 세이지 않고 "sw 할당자 밖" 몫으로 보인다.
        // 플랫폼 누수 추적은 그보다도 먼저 켠다(Windows Debug CRT: 할당 추적 + 보고를 stderr 로). `EngineLoop` 가 기동 뒤 기준선을 찍고
        // 종료 뒤 `reportMemoryLeaks` 로 비교한다 — 켜지 않으면 누수 덤프가 디버거 출력으로만 나가 콘솔 · CI 에서 보이지 않는다.
        // 메모리 프로파일러는 배포본이 아닌 모든 구성에 선다(태그 · 최고치 · 예산 · 보고). 추적은 진단 구성(Debug · 시험)에서 켜 두고, Release 는 꺼 둔 채
        // `-gv_memoryTracking=1` 로 켠다 — 꺼져 있으면 할당마다 분기 하나다. 플랫폼 누수 검사는 진단 구성만.
        if ( bDiagnostics )
            MemoryProfiler::enableMemoryLeakChecks();
        // 배포본에는 할당 헤더가 없어 셀 것이 없다 — 시험 하네스(진단 구성)만 같은 API 를 쓰려고 세운다.
#if defined( SW_SHIPPING )
        const bool bCreateMemoryProfiler = bDiagnostics;
#else
        const bool bCreateMemoryProfiler = true;
#endif
        if ( bCreateMemoryProfiler )
        {
            _memoryProfiler = make_unique<MemoryProfiler>();
            _memoryProfiler->initialize();
            _memoryProfiler->setTrackingEnabled( bDiagnostics );
        }
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
        // 심볼과 짝짓는 열쇠 — 실행 파일과(다르면) 엔진 모듈의 빌드 id. 덤프의 모듈 목록에도 같은 값이 있다.
        const ModuleBuildId executableId = ModuleBuildId::find( nullptr );
        const ModuleBuildId engineId     = ModuleBuildId::find( reinterpret_cast<const void*>( &CrashHandler::setContextValue ) );
        CrashHandler::setContextValue( "BuildId", executableId._id );
        if ( engineId._id != executableId._id )
            CrashHandler::setContextValue( "EngineBuildId", engineId._id );

        if ( bDiagnostics )
        {
            _deadlockDetector = make_unique<DeadlockDetector>();
            _deadlockDetector->initialize();
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

        // 프로세스 정적 저장소가 기동 뒤 자란 몫을 돌려준다 — 남기면 아래 종료 보고(기준선 대비 태그 증가)에 남는다. 이름 풀은 아래에서 내린다.
        // 컴포넌트는 모두 사라진 뒤다. 칸이 남았으면 그 컴포넌트가 새는 것이라 놓지 않고 알린다(페이지를 놓으면 그 컴포넌트가 내려간 메모리를 든다).
        SceneTransformStorage& transformStorage = SceneTransformStorage::get();
        if ( transformStorage.releaseStorage() == false )
            SW_LOG_WARNING( "Scene transform storage still has %# live slots at shutdown - a scene component leaked", transformStorage.getLiveSlotCount() );
        ResourceUtil::clearPathCache();

        // 로거 **스레드**는 메모리 프로파일러보다 먼저 세운다. 그 스레드도 메모리를 풀며 프로파일러를 부른다(`Memory::free` → `recordFree`).
        // 로거 객체는 맨 마지막에 놓는다 — 그 사이의 로그는 출력에 바로 쓰인다.
        if ( _logger != nullptr )
            _logger->shutdown();
        if ( _deadlockDetector != nullptr )
            _deadlockDetector->shutdown();
        _deadlockDetector.reset();

        // 이름 풀은 hashed_string 을 든 객체가 모두 사라진 뒤에 내린다.
        HashedStringPool::shutdown();
        CrashHandler::shutdown();
        _logger.reset();

        // 프로파일러는 맨 끝이다 — 위의 해제가 모두 태그 줄에서 빠진 뒤에 기동 뒤 기준선과 견준다(어느 용도가 남았나, Debug). 플랫폼 누수 검사(CRT)는
        // 바이트만 말하고, 이 보고는 그 바이트가 어느 하위 시스템의 것인지 말한다.
        if ( _memoryProfiler != nullptr )
        {
#if defined( SW_DEBUG )
            (void)_memoryProfiler->reportTagGrowthSinceBaseline( "shutdown" );
#endif
            _memoryProfiler->shutdown();
        }
        _memoryProfiler.reset();
    }
} // namespace sw
