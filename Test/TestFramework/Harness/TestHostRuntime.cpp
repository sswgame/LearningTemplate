#include "pch.h"

#include "TestFramework/Harness/TestHostRuntime.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Audio/NullAudioBackend.h"
#include "Engine/Bootstrap/DiagnosticsSubsystem.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/EngineSubsystemSet.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Module/ReflectionSubsystem.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

#if defined( SW_SHIPPING )
    #include "sw/config/ShippingGameDefaults.h"
#endif

namespace
{
    /** @brief 하네스의 Reflection — 엔진 타입에 더해 GameFramework · 테스트 타입까지 모은다(테스트 실행 파일과 함께 올라왔다). */
    class TestReflectionSubsystem final : public sw::ReflectionSubsystem
    {
    public:
        using sw::ReflectionSubsystem::ReflectionSubsystem;

        sw::SubsystemInitResult initializeSubsystem() override
        {
            sw::engine::registerModuleTypes( "Engine" );
            sw::engine::registerModuleTypes( "GameFramework" );
            getTypeRegistry().registerPendingTypes( "TestFramework", sw::TypeRegistrar::getHead(), sw::EnumRegistrar::getHead() );
            return bindEnumNames();
        }
    };
    /** @brief 하네스는 오디오 장치를 열지 않는다(초기화 없음) — 종료는 앱과 같다(초기화하지 않았으니 할 일이 없다). */
    class TestAudioService final : public sw::NullAudioBackend
    {
    public:
        sw::SubsystemInitResult initializeSubsystem() override { return sw::SubsystemInitResult::Succeeded; }
    };

    /**
     * @brief 하네스는 엔진 기본 에셋 표만 읽는다(문화권 표 · 로컬라이제이션 프로젝트는 걸지 않는다).
     * @details 셰이더 · 파이프라인 경로는 데이터(enginedefaultassets.xml)에만 있다 — 읽지 못하면 렌더 테스트가 쓸 경로가 없으므로 하네스 기동을 멈춘다.
     */
    class TestEngineDefaultAssetsSubsystem final : public sw::EngineDefaultAssetsSubsystem
    {
    public:
        using sw::EngineDefaultAssetsSubsystem::EngineDefaultAssetsSubsystem;

        sw::SubsystemInitResult initializeSubsystem() override
        {
            return getDefaultAssets().loadFromResource() ? sw::SubsystemInitResult::Succeeded : sw::SubsystemInitResult::Failed;
        }
    };
} // namespace

namespace test
{
    const sw::EngineDefaultAssets& getEngineDefaultAssets()
    {
        return sw::engine::getSubsystem<sw::EngineDefaultAssets>();
    }

    sw::GameConfig makeGameConfig( sw::string_view packRoot )
    {
        sw::GameConfig                                 config;
        sw::StringBuilder<sw::constant::kMaxBuffer512> json;
        json.append( R"({ "_packRoot": ")" ).append( packRoot ).append( R"(" })" );
        const bool bRead = sw::ConfigManager::readConfigJSON( config, sw::string_view( json.c_str(), json.size() ), "test::makeGameConfig" );
        SW_ASSERT( bRead );
        (void)bRead;
        return config;
    }

    const utf8* rebindEngineServices( const sw::ISubsystemLocator& subsystems )
    {
        // 이 파일이 호스트다(아래 `TestHostRuntime::start` 가 표를 묶는다). 테스트는 이 창구로만 표를 갈아 끼운다 — 하네스의 엔진 범위를 부모로 둔
        // 범위에 바꿀 서비스만 넣어 묶으면 그 이름이 부모의 것을 가린다.
        return sw::engine::bindEngineServices( subsystems );
    }

    ScopedEngineServiceOverride::ScopedEngineServiceOverride()
        : _pSaved{ sw::engine::getEngineSubsystems() }
        , _overrides{}
    {
        _overrides.setParentScope( _pSaved );
    }

    ScopedEngineServiceOverride::~ScopedEngineServiceOverride()
    {
        if ( _pSaved != nullptr )
            (void)rebindEngineServices( *_pSaved ); // 들어올 때의 범위다 — 그때 빠진 것이 없었으면 지금도 없다
        else
            sw::engine::unbindEngineServices();
    }

    const utf8* ScopedEngineServiceOverride::bind()
    {
        return rebindEngineServices( _overrides );
    }

    ScopedUnboundEngineServices::ScopedUnboundEngineServices()
        : _pSaved{ sw::engine::getEngineSubsystems() }
    {
        sw::engine::unbindEngineServices();
    }

    ScopedUnboundEngineServices::~ScopedUnboundEngineServices()
    {
        if ( _pSaved != nullptr )
            (void)rebindEngineServices( *_pSaved ); // 들어올 때의 범위다
    }
} // namespace test

namespace test
{
    /** @brief 하네스 하나의 상태입니다. 선언 순서가 소멸 순서의 역이다 — 부트스트랩 → 서비스 → 엔진 범위(엔진 루프와 같다). */
    struct TestHostRuntime::State
    {
        sw::HeadlessTaskState _headlessState{}; // 기동 범위보다 오래 산다(헤드리스 단계가 결과를 적는다)
        sw::SubsystemScope    _bootstrap{};
        sw::SubsystemScope    _services{};
        sw::SubsystemScope    _startup{};
        bool                  _bBootstrapped{ false };
    };

    TestHostRuntime::TestHostRuntime()
        : _pState{}
    {
    }

    TestHostRuntime::~TestHostRuntime()
    {
        stop();
    }

    bool TestHostRuntime::start( int32 argc, utf8* argv[] )
    {
        if ( _pState != nullptr )
            return false;
        _pState      = sw::make_unique<State>();
        State& state = *_pState;

        // ------------------------------------------------------------------------------
        // 0) 부트스트랩 범위 — `Engine` 과 같은 것(진단 도구 · 이름 풀 · 로거 · 명령줄 · 크래시 핸들러 · 정적 저장소)
        // ------------------------------------------------------------------------------
        // 테스트 실행 파일은 진단 도구(교착 감지기 · 메모리 프로파일러)를 늘 켠다.
        // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다(명령줄 서브시스템이 나머지를 파싱한다).
        sw::vector<utf8*> listApplicationArg = TestRegistry::getInstance().configureFromArgs( argc, argv );
        if ( TestRegistry::getInstance().isHelpRequested() )
        {
            // 도움말은 엔진 없이 찍는다 — 기동을 기다릴 까닭이 없고, `main` 이 종료 코드 0 으로 끝낸다.
            TestRunOptions::printHelp();
            return false;
        }
        state._bBootstrapped = true;
        {
            sw::SubsystemScope&        bootstrap = state._bootstrap;
            sw::engine::BootstrapSetup bootstrapSetup;
            bootstrapSetup._argc                         = static_cast<int32>( listApplicationArg.size() );
            bootstrapSetup._ppArgv                       = listApplicationArg.data();
            bootstrapSetup._bDiagnostics                 = true; // 늘 진단 구성이다 — 메모리 예산은 두지 않고 진단 구성의 추적을 그대로 둔다
            const sw::engine::BootstrapParts parts       = sw::engine::addBootstrapSubsystems( bootstrap, bootstrapSetup );
            const sw::DiagnosticsSubsystem&  diagnostics = *parts._pDiagnostics;
            if ( bootstrap.getCollection().initializeAll( sw::SubsystemHostMask::All ) == false )
            {
                std::fprintf( stderr, "[TestHost] bootstrap failed: %s\n", bootstrap.getCollection().getError().c_str() );
                return false;
            }
            (void)bootstrap.bind( diagnostics.findMemoryProfiler() ); // 하네스는 늘 진단 구성이라 있다 — 새 범위라 겹치지 않는다
        }

        // ------------------------------------------------------------------------------
        // 1) 서비스 범위 — 엔진 범위보다 오래 사는 공용 서비스(엔진 루프와 같다. 하네스는 배포본 테스트에도 커맨드 스택을 둔다)
        // ------------------------------------------------------------------------------
        {
            sw::SubsystemScope& services = state._services;
            services.setParentScope( &state._bootstrap );
            sw::engine::ServiceSetup serviceSetup;
            serviceSetup._bEditorServices = true; // 하네스는 배포본 테스트에도 커맨드 스택을 둔다
            (void)sw::engine::addServiceSubsystems( services, serviceSetup );
            if ( services.getCollection().initializeAll( sw::EngineSubsystemHost::kClient ) == false )
            {
                std::fprintf( stderr, "[TestHost] services failed: %s\n", services.getCollection().getError().c_str() );
                return false;
            }
        }

        // ------------------------------------------------------------------------------
        // 2) 엔진 범위 — 리플렉션 · 설정 · 리소스 · 태스크 · 입력 · 씬
        // ------------------------------------------------------------------------------
        // 서브시스템 목록은 엔진 루프와 같은 함수(`EngineSubsystemSet`)다. 하네스가 다른 것은 아래 값뿐이다 — 외부 코덱을 붙이지 않고(테스트가 코덱을 직접 갈아 끼운다),
        // 렌더러 단계를 두지 않으며, 리플렉션 · 기본 에셋 · 오디오는 본문만 다른 대역이다. 글꼴 · UI · 텔레메트리 · 사용자 설정은 서비스 표에만 있고 서지 않는다
        // (Client 전용 — 글꼴 · UI 테스트는 자기 FontService · UISystem 을 만든다). 그래서 그 넷은 하네스만의 호스트 비트(`kTestHarness`)로 거른다.
        sw::SubsystemScope& startup = state._startup;
        startup.setParentScope( &state._services );
        // 만드는 동안 그 서브시스템의 메모리 용도를 건다 — 생성자가 잡는 표 · 풀이 그 줄로 세인다(앱과 같다).
        sw::unique_ptr<sw::ReflectionSubsystem> reflection;
        {
            SW_MEMORY_SCOPE( Reflection );
            reflection = sw::make_unique<TestReflectionSubsystem>( sw::make_unique<sw::TypeRegistry>() );
        }
        sw::engine::StartupSetup startupSetup{ state._headlessState };
        startupSetup._reflection    = std::move( reflection );
        startupSetup._defaultAssets = sw::make_unique<TestEngineDefaultAssetsSubsystem>();
        startupSetup._audio         = sw::make_unique<TestAudioService>();
        (void)sw::engine::addStartupSubsystems( startup, std::move( startupSetup ) ); // 렌더러 단계는 두지 않는다(`_bRenderer` 기본 false)
        if ( const utf8* pMissing = sw::engine::bindEngineServices( startup ) )
        {
            std::fprintf( stderr, "[TestHost] required engine service '%s' is not in any subsystem scope\n", pMissing );
            return false;
        }
        if ( startup.getCollection().initializeAll( sw::EngineSubsystemHost::kTestHarness ) )
            return true;
        // 하네스 기동 실패는 표준 오류로 알린다 — 정렬 오류(선언 오타 · 순환)는 로거가 무엇이든 같은 길로 보인다.
        std::fprintf( stderr, "[TestHost] startup failed: %s\n", startup.getCollection().getError().c_str() );
        return false;
    }

    void TestHostRuntime::stop()
    {
        if ( _pState == nullptr )
            return;
        // 3) 종료 — 엔진 범위 종료 · 해제(표의 역순), 그 뒤 서비스 · 부트스트랩 범위(`Engine::shutdown` 과 같은 끝 정리). 기동이 중간에 졌어도 같은 길이다.
        if ( _pState->_bBootstrapped )
        {
            // 단계 앞마다 한 줄 — 종료에서 멈춘 실행(요약은 찍혔는데 마지막 `shut down` 줄이 없다)이 어느 단계에서 멈췄는지 로그에 남는다.
            // 로그 매크로는 Shipping 에서 빠지므로 표준 출력으로 찍는다.
            std::fprintf( stdout, "[TestHost] stopping: startup steps\n" );
            std::fflush( stdout );
            _pState->_startup.getCollection().shutdownAll();
            std::fprintf( stdout, "[TestHost] stopping: destroying objects\n" );
            std::fflush( stdout );
            _pState->_startup.destroyAll();
            std::fprintf( stdout, "[TestHost] stopping: services and bootstrap\n" );
            std::fflush( stdout );
            _pState->_services.destroyAll();
            sw::engine::unbindEngineServices();
            _pState->_bootstrap.destroyAll();
        }
        _pState.reset();
    }
} // namespace test
