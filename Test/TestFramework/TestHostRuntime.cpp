#include "pch.h"

#include "TestFramework/TestHostRuntime.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Subsystem/SubsystemCollection.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Audio/NullAudioBackend.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/EngineBootstrap.h"
#include "Engine/EngineServiceCollection.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Graphics/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Localization/EngineDefaultAssetsSubsystem.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/StringTable.h"
#include "Engine/Module/ModuleImagesSubsystem.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Module/ModuleTypesSubsystem.h"
#include "Engine/Module/ReflectionSubsystem.h"
#include "Engine/Object/Component/ComponentDefaults.h"
#include "Engine/Object/GameObject/SceneEngineServices.h"
#include "Engine/Physics/PhysicsSystem.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/Format/JSONSerializer.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Utility/DebugOverlayState.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

#include "sw/config/ConfigConstants.h"
#include "sw/config/ShippingHostDefaults.h"

#if defined( SW_SHIPPING )
    #include "sw/config/ShippingGameDefaults.h"
#endif

namespace
{
    /**
     * @brief 하네스의 호스트 상태입니다. 순서는 `EngineLoop` 과 같은 서브시스템 클래스가 스스로 선언한 의존이 정합니다.
     * @details 하네스는 헤드리스 작업 · 플레이어 설정 · RHI · 렌더러를 세우지 않으므로 그 단계를 넣지 않는다(그것에 기대는 단계도 없다).
     *          서브시스템은 `TestHostRuntime::start` 가 앱과 같은 선언 · 같은 객체로 넣고, 본문이 다른 것만 하네스가 준다
     *          (글꼴 · UI 테스트는 자기 FontService · UISystem 을 만든다). 오디오는 초기화하지 않고 종료만 부른다. 해제는 `EngineLoop` 과 같은 역순이다.
     */
    struct TestHost
    {
        sw::unique_ptr<sw::IAudioService> _audioService{}; ///< 만든 뒤 서비스 표에 묶고 소유를 Audio 서브시스템으로 넘긴다
        sw::unique_ptr<sw::CommandStack>  _commandStack{}; ///< 서비스 표에 묶는다 — 씬 매니저가 씬이 바뀌면 비운다. 서브시스템이 모두 소멸한 뒤 놓는다
    };
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

    /** @brief 하네스는 엔진 데이터를 읽지 않는다 — 저장소가 만든 기본값을 소유만 한다. */
    class TestEngineDefaultAssetsSubsystem final : public sw::EngineDefaultAssetsSubsystem
    {
    public:
        using sw::EngineDefaultAssetsSubsystem::EngineDefaultAssetsSubsystem;

        sw::SubsystemInitResult initializeSubsystem() override { return sw::SubsystemInitResult::Succeeded; }
    };
} // namespace

namespace test
{
    void rebindEngineServices( const sw::EngineServices& services )
    {
        // 이 파일이 호스트다(아래 `TestHostRuntime::start` 가 표를 채운다). 테스트는 이 창구로만 표를 갈아 끼운다.
        sw::engine::bindEngineServices( services );
    }
} // namespace test

namespace test
{
    /** @brief 하네스 하나의 상태입니다. 선언 순서가 소멸 순서의 역이다 — 저장소 → 부트스트랩 → 호스트 → 기동 단계. */
    struct TestHostRuntime::State
    {
        sw::EngineServiceCollection _owned{};
        sw::EngineBootstrap         _bootstrap{};
        TestHost                    _host{};
        sw::SubsystemCollection     _startup{};
        bool                        _bBootstrapped{ false };
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
        _pState         = sw::make_unique<State>();
        State&    state = *_pState;
        TestHost& host  = state._host;

        // ------------------------------------------------------------------------------
        // 0) 부트스트랩 — `EngineLoop` 과 같은 것(이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 진단 도구 · 명령줄 · 전역 변수)
        // ------------------------------------------------------------------------------
        // 목록(`EngineServiceList.xxx`)의 `EngineCreated` 서비스는 저장소가 만든다. 테스트 실행 파일은 진단 도구(교착 감지기 · 메모리 프로파일러)를 늘 켠다.
        // 프레임워크 전용 플래그를 먼저 소비해 CommandLineManager 가 미지 인자를 경고하지 않게 한다(부트스트랩이 나머지를 파싱한다).
        sw::vector<utf8*> listApplicationArg = TestRegistry::getInstance().configureFromArgs( argc, argv );
        if ( TestRegistry::getInstance().isHelpRequested() )
        {
            // 도움말은 엔진 없이 찍는다 — 기동을 기다릴 까닭이 없고, `main` 이 종료 코드 0 으로 끝낸다.
            TestRegistry::getInstance().printHelp();
            return false;
        }
        if ( state._bootstrap.initialize( state._owned, true, static_cast<int32>( listApplicationArg.size() ), listApplicationArg.data() ) == false )
            return false;
        state._bBootstrapped = true;

        state._owned.createAll();
        host._audioService = sw::make_unique<TestAudioService>();
        host._commandStack = sw::make_unique<sw::CommandStack>();

        sw::EngineServices services{};
        state._bootstrap.fillServices( services );
        // HostCreated 인 자리만 손으로 꽂는다(팩토리 · 구성별 조건부).
        services._pAudioService = host._audioService.get();
        services._pCommandStack = host._commandStack.get();
        sw::engine::bindEngineServices( services );

        // ------------------------------------------------------------------------------
        // 1) 기동 단계 — 리플렉션 · 설정 · 리소스 · 태스크 · 입력 · 씬
        // ------------------------------------------------------------------------------
        // 순서는 손으로 적지 않는다. `EngineLoop` 과 같은 서브시스템 클래스를 그 의존으로 위상 정렬한 순서로 세운다 —
        // 하네스가 앱과 다른 순서로 서는 일이 구조로 막힌다. 본문이 다른 것만 여기서 준다. 종료와 해제는 `stop` 이 그 역순으로 한다.
        sw::SubsystemCollection& startup = state._startup;
        // 앱과 달리 외부 코덱을 붙이지 않는다(테스트가 코덱을 직접 갈아 끼운다). 서비스와 **같은 인스턴스**가 Core 슬롯에도 꽂힌다(레지스트리의 초기화).
        startup.add( std::move( state._owned._pCompressionCodecRegistry ) );
        startup.add( sw::make_unique<TestReflectionSubsystem>( std::move( state._owned._pTypeRegistry ) ) );
        startup.add( std::move( state._owned._pTaskManager ) );
        {
            sw::AsyncFileIOSettings fileIOSettings{};
            fileIOSettings._pTaskManager = &sw::engine::getTaskManager();
            state._owned._pAsyncFileIO->setSubsystemSettings( fileIOSettings );
            startup.add( std::move( state._owned._pAsyncFileIO ) );
        }
        // 하네스는 모듈 DLL 을 올리고 내리지 않는다(훅 없이 자리만). 타입 공급자는 리플렉션 단계가 이미 모았다 — 로더 없이 등록을 끝낸 것으로 적는다.
        startup.add( sw::make_unique<sw::ModuleImagesSubsystem>() );
        startup.add( sw::make_unique<sw::ModuleTypesSubsystem>() );
        startup.add( std::move( state._owned._pInputManager ) );
        // 설정 · 리소스는 앱과 같은 서브시스템이다(설정이 먼저 — `loadAssetRegistries` 가 게임 레지스트리 경로를 GameConfig 로 만든다).
        startup.add( sw::make_unique<sw::ConfigManager>() );
        startup.add( std::move( state._owned._pAssetManager ) );
        startup.add( sw::make_unique<TestEngineDefaultAssetsSubsystem>( std::move( state._owned._pEngineDefaultAssets ) ) );
        startup.add( std::move( host._audioService ) );
        startup.add( std::move( state._owned._pAssetStreamingQueue ) );
        startup.add( std::move( state._owned._pSceneManager ) );
        startup.add( std::move( state._owned._pShaderCache ) );
        startup.add( std::move( state._owned._pPhysicsSystem ) );
        if ( startup.initializeAll( sw::EngineSubsystemHost::kClient ) )
            return true;
        // 하네스 기동 실패는 표준 오류로 알린다 — 정렬 오류(선언 오타 · 순환)는 로거가 무엇이든 같은 길로 보인다.
        std::fprintf( stderr, "[TestHost] startup failed: %s\n", startup.getError().c_str() );
        return false;
    }

    void TestHostRuntime::stop()
    {
        if ( _pState == nullptr )
            return;
        // 2) 종료 — 단계 종료 · 해제(표의 역순), 그 뒤 부트스트랩(`EngineLoop::shutdown` 과 같은 끝 정리). 기동이 중간에 졌어도 같은 길이다.
        if ( _pState->_bBootstrapped )
        {
            // 단계 앞마다 한 줄 — 종료에서 멈춘 실행(요약은 찍혔는데 마지막 `shut down` 줄이 없다)이 어느 단계에서 멈췄는지 로그에 남는다.
            // 로그 매크로는 Shipping 에서 빠지므로 표준 출력으로 찍는다.
            std::fprintf( stdout, "[TestHost] stopping: startup steps\n" );
            std::fflush( stdout );
            _pState->_startup.shutdownAll();
            std::fprintf( stdout, "[TestHost] stopping: destroying objects\n" );
            std::fflush( stdout );
            _pState->_startup.destroyAll();
            _pState->_host._commandStack.reset(); // 씬 매니저가 구독을 뗀 뒤 — 앱과 같다
            std::fprintf( stdout, "[TestHost] stopping: bootstrap\n" );
            std::fflush( stdout );
            _pState->_bootstrap.shutdown();
        }
        _pState.reset();
    }
} // namespace test
