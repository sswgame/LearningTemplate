#include "pch.h"

#include "ModuleManager/Host/HostLaunch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineSubsystems.h"

#include "ModuleManager/GameModuleManager.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 모르는 `-gv_*` 키를 경고합니다. 파서는 모듈이 선언하기 전이라 모르는 키를 보류 테이블에 남긴다 — 모두 선 뒤에도 가져간 곳이 없으면 오타다.
         * @details 테이블은 비우지 않는다. 핫 리로드로 나중에 올라오는 모듈이 여전히 가져갈 수 있다.
         */
        void warnUnknownGlobalOverrides( const CommandLineManager& commandLine )
        {
            const vector<string> listPendingName = commandLine.collectPendingGlobalNames();
            for ( const string& pendingName : listPendingName )
            {
                if ( engine::getSubsystem<GlobalVariableManager>().findVariable( pendingName ) == nullptr )
                    SW_LOG_WARNING_ID( "HostServices.NoSuchGlobalVariable", "%#: no such global variable. Ignored", pendingName.c_str() );
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    HostLaunch::HostLaunch()
        : _host{}
        , _pModuleLoader{ nullptr }
        , _pDevLayer{ nullptr }
        , _engine{}
        , _moduleManager{ nullptr }
        , _moduleCatalog{}
        , _moduleResolution{}
        , _hostServices{}
    {
    }

    HostLaunch::~HostLaunch() = default;

    bool HostLaunch::initializeHostScope( ModuleTypeLoaderDelegate moduleTypeLoader, const utf8* pHostName, unique_ptr<IHostDevLayer> pDevLayer )
    {
        // 개발 도구(엔진 기동 확장 · 콘텐츠 폴더 감시). 모듈 맡김 칸(`IModuleStateStore`)은 범위를 세우는 쪽(`ModuleLoaderSubsystem::initializeHostScope`)이 건다.
        // 개발 계층은 개발 구성에만 있고 배포에는 없다 — 어긋나면 폴백 없이 멈춘다(개발 구성인데 핫 리로드 · 개발 도구 없이 서지 않는다).
        if ( ( pDevLayer != nullptr ) == build::kShipping )
        {
            SW_LOW_LEVEL_ERROR( "[%s] the dev layer must be given in development builds and absent in shipping (given: %d)", pHostName, pDevLayer != nullptr ? 1 : 0 ); // 로거 앞이다
            return false;
        }
        _pDevLayer = std::move( pDevLayer );
        if ( _pDevLayer != nullptr )
            _pDevLayer->addToHostScope( _host, _engine );
        _pModuleLoader = ModuleLoaderSubsystem::initializeHostScope( _host, std::move( moduleTypeLoader ), pHostName );
        return _pModuleLoader != nullptr;
    }

    HostEngineStart HostLaunch::initializeEngine( int32 argc, utf8* pArgv[], EngineHostRole role )
    {
        if ( _engine.initialize( argc, pArgv, role, &_host ) == false )
        {
            SW_LOG_ERROR_ID( "HostLaunch.EngineInitializationFailed", "Engine initialization failed" );
            return HostEngineStart::Failed;
        }
        // 엔진이 섰다 — 개발 도구의 프레임 훅을 붙인다(씬 틱 뒤 HUD · 콘솔, 프레임 끝에 셰이더 재컴파일 · 단축키 · 설정 리로드).
        if ( _pDevLayer != nullptr )
            _pDevLayer->attachFrameHooks( _engine );
        // 작업이 실패했으면 **기동 실패다.** 그래야 종료 코드가 0 이 아니고, 이 프로세스를 실행하는 빌드(CookAssets 타깃) · `pack.py` 가 "쿠킹하지 못했다" 는 사실을 알 수 있다.
        if ( _engine.isHeadless() )
            return _engine.didHeadlessTaskFail() ? HostEngineStart::Failed : HostEngineStart::Headless;
        return HostEngineStart::Ready;
    }

    bool HostLaunch::loadModuleLibraries( uint8 targetMask, unique_ptr<GameModuleManager> moduleManager )
    {
        // 모듈 호스트 · 감시자 · 매니페스트는 엔진 기반의 일이다. 모듈 본체의 로드는 모듈 매니저가 그 모듈 종류의 태그로 건다.
        const ScopedMemoryTag memoryScope{ kMemoryTag };
        // 모듈 매니페스트 · 핫 리로드 구동기는 동적 모듈 구성만이다 — 정적 구성(배포 · 웹 · Android 개발)은 모듈을 정적으로 링크해 아무것도 하지 않는다.
#if SW_WITH_DYNAMIC_MODULES
        if ( _pDevLayer != nullptr && _pDevLayer->initializeModuleReload( targetMask, _host, *_pModuleLoader, _moduleCatalog, _moduleResolution ) == false )
            return false;
#else
        (void)targetMask;
#endif
        _moduleManager = std::move( moduleManager );
        return _moduleManager->loadModuleLibraries( getLiveReloadManager(), _moduleCatalog, _moduleResolution );
    }

    bool HostLaunch::startHostServices()
    {
        const CommandLineManager* pCommandLine = _engine.getCommandLineManager();
        if ( pCommandLine == nullptr )
            return false;
        if ( _hostServices.initializeAll( SubsystemHostMask::All ) == false )
        {
            SW_LOG_ERROR_ID( "HostServices.HostServicesFailedStart", "Host services failed to start: %#", _hostServices.getError().c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "HostServices.HostServicesUp", "Host services up: %#", describeHostServiceOrder().c_str() );
        // 모듈 · 게임이 모두 선 지금까지 가져간 곳이 없는 `-gv_*` 는 오타다.
        warnUnknownGlobalOverrides( *pCommandLine );
        return true;
    }

    string HostLaunch::describeHostServiceOrder() const
    {
        string text;
        for ( const uint32 index : _hostServices.getInitializedIndices() )
        {
            if ( text.empty() == false )
                text += " > ";
            text += _hostServices.getSubsystem( index ).getSubsystemName();
        }
        return text;
    }

    void HostLaunch::updateModuleReload()
    {
#if SW_WITH_DYNAMIC_MODULES
        if ( _pDevLayer != nullptr )
            _pDevLayer->updateModuleReload();
#endif
    }

    void HostLaunch::stopHostServices()
    {
        _hostServices.destroyAll();
        // 헤드리스 부팅은 라이브러리만 올리고 호스트 서비스(게임)을 세우지 않는다 — 라이브러리를 쥔 모듈 관리자는 엔진(모듈 라이브러리 종료 단계)보다 먼저 놓는다.
        _moduleManager.reset();
    }

    void HostLaunch::releaseModuleManager()
    {
        _moduleManager.reset();
    }

    void HostLaunch::shutdownEngine()
    {
        if ( _pDevLayer != nullptr )
            _pDevLayer->detach(); // 개발 도구는 엔진 서비스를 빌려 쓴다 — 엔진보다 먼저 뗀다
        _engine.shutdown( _host );
        _pModuleLoader = nullptr;
    }
} // namespace sw
