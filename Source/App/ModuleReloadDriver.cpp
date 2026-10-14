#include "pch.h"

#include "App/ModuleReloadDriver.h"

#include "App/AppAutomation.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Engine.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Profiling/FrameProfiler.h"

#include "ModuleHost/ModuleImageHostSubsystem.h"

#include "sw/config/ConfigConstants.h"

#if !defined( SW_SHIPPING )
    #include "ModuleHost/LiveReloadManager.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "App" );

#if !defined( SW_SHIPPING )
    namespace
    {
        /** @brief 에디터 모듈(EditorModule) 핫 리로드 단축키의 액션 이름(`default.input.xml` 의 Debug 레이어). 에디터를 아는 App 만 쓴다. */
        inline constexpr utf8 kReloadEditorAction[] = "ReloadEditor";
    } // namespace

    /**
     * @brief `-gv_reloadGameAtFrame=N`: N 번째 프레임에 게임 모듈(SWGame)의 핫 리로드를 한 번 요청합니다(리로드 단축키와 같은 경로).
     * @details 디렉터의 시뮬레이션이 리로드 뒤에도 이어지는지 손대지 않고 확인하는 스위치입니다. 로그의 `[ReloadProbe]` 앞뒤 상태 줄을 비교합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_reloadGameAtFrame, 0, "이 프레임에 게임 모듈 핫 리로드를 요청한다 (0=사용 안 함)" );
#endif

    ModuleReloadDriver::ModuleReloadDriver()
#if !defined( SW_SHIPPING )
        : _liveReloadManager{ nullptr }
#endif
    {
    }

    ModuleReloadDriver::~ModuleReloadDriver() = default;

    void ModuleReloadDriver::initialize( [[maybe_unused]] ModuleImageHostSubsystem& moduleImageHost )
    {
#if !defined( SW_SHIPPING )
        _liveReloadManager = make_unique<LiveReloadManager>();
        AppAutomation::bindLiveReloadManager( _liveReloadManager.get() ); // 개발 명령 `module.reload` · 탐침 `Module.*` 가 읽는다
        // 모듈 DLL 을 언로드할 수 있는 곳은 "씬은 사라졌고 서비스는 아직 있는" 좁은 구간뿐이다. 호스트 범위의 모듈 이미지 창구가 엔진 종료 안에서 그 자리를 부른다.
        moduleImageHost.setScenesReleasedHook( SW_DELEGATE_LAMBDA( Delegate<void()>, [this]()
        {
            AppAutomation::bindLiveReloadManager( nullptr );
            if ( _liveReloadManager != nullptr )
                _liveReloadManager->shutdown();
        } ) );
#endif
    }

    void ModuleReloadDriver::pollReloadTriggers( [[maybe_unused]] const Engine& engine, [[maybe_unused]] bool bEnableEditor )
    {
#if !defined( SW_SHIPPING )
        // 모듈을 다시 로드하는 장치는 여기(App)에 있다. Engine 에는 "이 액션이 눌렸나"만 묻는다.
        if ( engine.wasDebugActionTriggered( InputMapDefaults::kReloadGameAction ) )
        {
            requestReload( config::kTargetGameModule );
            SW_LOG_INFO( "%#: force SWGame reload", InputMapDefaults::kReloadGameAction );
        }
        if ( gv_reloadGameAtFrame > 0 && engine::getSubsystem<FrameProfiler>().getFrameCount() == static_cast<uint64>( gv_reloadGameAtFrame ) )
        {
            SW_LOG_INFO( "[ReloadProbe] frame %# - forcing SWGame reload", gv_reloadGameAtFrame );
            gv_reloadGameAtFrame = 0; // 한 번만 실행한다. 프로파일러가 프레임 수를 되돌리면(워밍업 뒤) 같은 번호가 다시 온다.
            requestReload( config::kTargetGameModule );
        }
        if ( bEnableEditor && engine.wasDebugActionTriggered( kReloadEditorAction ) )
        {
            requestReload( config::kTargetEditorModule );
            SW_LOG_INFO( "%#: force EditorModule reload", kReloadEditorAction );
        }
#endif
        // Shipping 에는 리로드할 모듈이 없다. 그 입력 상태를 묻는 코드가 없으므로 셸 InputMap 도 로드하지 않는다.
    }

    void ModuleReloadDriver::update()
    {
#if !defined( SW_SHIPPING )
        if ( _liveReloadManager != nullptr )
            _liveReloadManager->update();
#endif
    }

    LiveReloadManager* ModuleReloadDriver::getLiveReloadManager() const
    {
#if defined( SW_SHIPPING )
        return nullptr;
#else
        return _liveReloadManager.get();
#endif
    }

    void ModuleReloadDriver::requestReload( [[maybe_unused]] const utf8* pModuleName )
    {
#if !defined( SW_SHIPPING )
        if ( _liveReloadManager != nullptr )
            _liveReloadManager->triggerReload( pModuleName );
#endif
    }
} // namespace sw
