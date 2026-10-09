#include "pch.h"

#if !defined( SW_SHIPPING )
    #include "App/Module/LiveReloadManager.h"
#endif
#include "App/Module/ModuleHost.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <thread>

using namespace sw;

// ------------------------------------------------------------------------------
// 1) ModuleHostTest — 디바이스가 없을 때의 호스트
//
// `RHI` 객체는 있는데 **디바이스가 없는 상태**가 실제로 존재한다: 백엔드 교체가 실패하면 디바이스만
// 사라지고 RHI 는 남는다(`RHIBackendSwitcher::applyPendingChange` 의 실패 경로). 그 뒤 종료가
// 돌면 호스트는 반드시 `drainRenderWorkers` 를 지나가는데, `RHI::getDevice()` 는 **널 참조**를
// 돌려주므로 `hasDevice()` 를 먼저 묻지 않으면 정상적인 실패가 종료 경로에서 SEGFAULT 로 끝난다.
//
// 이 스위트는 그 자리를 **디바이스 없는 RHI 하나로** 재현한다. GPU 도 창도 필요 없다.
// ------------------------------------------------------------------------------

/**
 * @brief [ModuleHostTest] 디바이스 없는 RHI 로도 초기화·종료가 죽지 않는다
 * @details 디바이스를 확인하지 않으면 `shutdown()` 안의 `drainRenderWorkers` 가 널 참조를 물어 프로세스가 죽는다.
 *          배포 구성에서는 초기화가 게임 인스턴스를 만들려다 같은 자리에서 죽는다.
 */
SW_TEST_CASE( ModuleHostTest, SurvivesAnRhiThatHasNoDevice )
{
    RHI rhi; // 디바이스 없음 — initialize() 를 부르지 않는다.
    SW_ASSERT_FALSE( rhi.hasDevice() );

    ModuleHost host;
    // 리로드 매니저·창·렌더 스레드 없이. 개발 구성은 등록할 모듈이 없어 그대로 성공하고,
    // 배포 구성은 정적 게임 API 를 묶다가 **디바이스가 없다는 것을 알고 멈춘다.**
    SW_EXPECT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, false ) );

    // 디바이스가 없으면 재생성도 거절한다 — 여기서 true 를 돌려주면 호출자가 인스턴스가 있다고 믿는다.
    SW_EXPECT_FALSE( host.reinitializeAfterRhiSwap( nullptr, nullptr ) );

    host.shutdown(); // 디바이스를 확인하지 않으면 여기서 죽는다.
}

#if !defined( SW_SHIPPING )
namespace
{
    /** @brief 가짜 에디터 API 가 불린 순서. */
    vector<const utf8*> s_listEditorCall;

    void recordStopSimulation( EditorHandle ) { s_listEditorCall.push_back( "stopSimulation" ); }
    void recordShutdown( EditorHandle ) { s_listEditorCall.push_back( "shutdown" ); }
    void recordDestroy( EditorHandle ) { s_listEditorCall.push_back( "destroy" ); }

    /** @brief 멈춤 · 종료 · 파괴만 채운 가짜 에디터 API 표. */
    EditorAPI makeRecordingEditorApi()
    {
        EditorAPI api{};
        api.stopSimulation = &recordStopSimulation;
        api.shutdown       = &recordShutdown;
        api.destroy        = &recordDestroy;
        return api;
    }
} // namespace

/**
 * @brief [ModuleHostTest] 모듈을 내릴 때(핫 리로드 · 백엔드 교체) 에디터 시뮬레이션을 **먼저** 멈춘다 — 게임만 내려도 그렇다
 * @details 월드 플레이 상태는 에디터보다 오래 사는 SceneManager 에 있다. 멈추지 않고 에디터를 내리면 새 에디터는 멈춤으로 시작하는데
 *          월드는 플레이 중으로 남는다(플레이 스냅샷도 되돌리지 않는다). 멈춤은 에디터 컨텍스트가 살아 있는 동안 — shutdown 앞이어야 한다.
 */
SW_TEST_CASE( ModuleHostTest, SuspendStopsTheEditorSimulationBeforeTearingDown )
{
    RHI rhi; // 디바이스 없음 — 워커 비우기는 그냥 지나간다.

    const ModuleScope arrScope[] = { ModuleScope::Editor, ModuleScope::Game, ModuleScope::Both };
    for ( const ModuleScope scope : arrScope )
    {
        ModuleHost host;
        SW_ASSERT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, true ) );
        int32 editorToken = 0;
        host.attachEditorInstance( makeRecordingEditorApi(), &editorToken );

        s_listEditorCall.clear();
        host.suspendModules( scope, false );

        SW_ASSERT_FALSE( s_listEditorCall.empty() );
        SW_EXPECT_STREQ( "stopSimulation", s_listEditorCall[0] );
        if ( scope != ModuleScope::Game )
        {
            // 에디터를 내리는 경우: 멈춤 → shutdown → destroy.
            SW_EXPECT_EQUAL( static_cast<size_t>( 3 ), s_listEditorCall.size() );
        }
        host.shutdown();
    }
}

/**
 * @brief [ModuleHostTest] 멈춤 창구가 없는 에디터를 내려도 월드가 플레이 중으로 남지 않는다
 */
SW_TEST_CASE( ModuleHostTest, SuspendingAnEditorWithoutStopLeavesTheWorldStopped )
{
    if ( engine::areEngineServicesBound() == false )
        SW_TEST_SKIP( "engine services are not bound in this executable" );

    RHI        rhi;
    ModuleHost host;
    SW_ASSERT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, true ) );
    EditorAPI api      = makeRecordingEditorApi();
    api.stopSimulation = nullptr;
    int32 editorToken  = 0;
    host.attachEditorInstance( api, &editorToken );

    SceneManager& sceneManager     = engine::getSceneManager();
    const bool    bWasWorldPlaying = sceneManager.isWorldPlaying();
    sceneManager.setWorldPlaying( true );
    host.suspendModules( ModuleScope::Editor, false );
    SW_EXPECT_FALSE( sceneManager.isWorldPlaying() );

    sceneManager.setWorldPlaying( bWasWorldPlaying );
    host.shutdown();
}

// 아래 케이스는 **개발 구성 전용**이다 — `LiveReloadManager` 는 배포 빌드에 아예 들어가지 않는다
// (모듈이 전부 정적 링크라 다시 올릴 것이 없다. `Test/SmokeTest/CMakeLists.txt` 의 소스 목록 참고).
/**
 * @brief [ModuleHostTest] 종료하면 등록부에 남은 콜백이 없다
 * @details 콜백은 `ModuleHost` 의 메서드를 가리킨다. `App` 은 호스트를 먼저 지우고 등록부를 나중에
 *          내리므로, 남아 있으면 그 사이의 리로드가 죽은 객체로 뛰어든다. 배수·배치 델리게이트뿐
 *          아니라 **모듈마다 건 것도** 떼야 한다.
 */
SW_TEST_CASE( ModuleHostTest, ShutdownDetachesEveryCallbackItRegistered )
{
    LiveReloadManager manager;
    if ( manager.registerModule( "SWGame" ) == false )
        SW_TEST_SKIP( "SWGame 모듈이 옆에 없습니다 (배포 구성 또는 빌드 산출물 없음)" );

    void* const initialHandle = manager.getModuleHandle( "SWGame" );
    SW_ASSERT_NOT_NULL( initialHandle );

    bool bBeforeCalled = false;
    manager.setOnBeforeReload( "SWGame", SW_DELEGATE_LAMBDA( LiveReloadManager::OnBeforeReloadDelegate, [&bBeforeCalled]()
    {
        bBeforeCalled = true;
    } ) );

    // 호스트가 자기 콜백을 뗄 때 쓰는 바로 그 창구다.
    manager.clearReloadCallbacks();

    // 디바운스(300ms)를 넘겨 **실제로 다시 올린다** — 리로드가 일어나지 않으면 "안 불렸다" 는
    // 아무것도 증명하지 못한다. 핸들이 바뀌는 것이 리로드가 돌았다는 증거다.
    manager.triggerReload( "SWGame" );
    for ( int32 stepIndex = 0; stepIndex < 80; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( manager.getModuleHandle( "SWGame" ) != initialHandle )
            break;
    }

    SW_EXPECT_TRUE_MSG( manager.getModuleHandle( "SWGame" ) != initialHandle,
                        "리로드가 일어나지 않았습니다 — 이 케이스는 아무것도 검증하지 못합니다" );
    SW_EXPECT_TRUE_MSG( bBeforeCalled == false, "뗀 콜백이 아직 불립니다 — 죽은 객체를 가리키는 자리다" );

    manager.shutdown();
}

namespace
{
    /** @brief 가짜 게임 API 가 불린 순서와, 상태 직렬화가 성공할지. */
    vector<const utf8*> s_listGameCall;
    bool                s_bGameSerializeSucceeds{ true };

    bool recordGameSerialize( GameHandle, void* pOutBuffer, uint32* pInOutSize )
    {
        s_listGameCall.push_back( "serializeState" );
        if ( pOutBuffer == nullptr && pInOutSize != nullptr )
            *pInOutSize = 4;
        return s_bGameSerializeSucceeds;
    }
    void recordGameShutdown( GameHandle ) { s_listGameCall.push_back( "shutdown" ); }
    void recordGameDestroy( GameHandle ) { s_listGameCall.push_back( "destroy" ); }

    /** @brief 상태 직렬화 · 종료 · 파괴만 채운 가짜 게임 API 표. */
    GameAPI makeRecordingGameApi()
    {
        GameAPI api{};
        api.serializeState = &recordGameSerialize;
        api.shutdown       = &recordGameShutdown;
        api.destroy        = &recordGameDestroy;
        return api;
    }

    /** @brief 모듈 타입 등록 해제를 알아보는 탐침 전역 변수입니다. 모듈 이름으로 올려 두면 `unregisterModuleTypes` 가 걷습니다. */
    int32                 s_teardownProbeValue{ 0 };
    constexpr const utf8* kEditorTeardownProbe = "gv_moduleHostEditorTeardownProbe";
    constexpr const utf8* kGameTeardownProbe   = "gv_moduleHostGameTeardownProbe";
    /** @brief 서비스를 뗄 때(`bindService( nullptr )`) 그 모듈의 탐침이 이미 걷혔었는지. */
    bool   s_bEditorTypesGoneAtUnbind{ false };
    bool   s_bGameTypesGoneAtUnbind{ false };
    uint32 s_editorUnbindCount{ 0 };
    uint32 s_gameUnbindCount{ 0 };

    void recordEditorBindService( const ModuleService* pService )
    {
        if ( pService != nullptr )
            return;
        ++s_editorUnbindCount;
        s_bEditorTypesGoneAtUnbind = engine::getGlobalVariableManager().findVariable( kEditorTeardownProbe ) == nullptr;
    }
    void recordGameBindService( const ModuleService* pService )
    {
        if ( pService != nullptr )
            return;
        ++s_gameUnbindCount;
        s_bGameTypesGoneAtUnbind = engine::getGlobalVariableManager().findVariable( kGameTeardownProbe ) == nullptr;
    }

    /** @brief @p pName 이 @p listCall 에 있는지 봅니다. */
    bool hasCall( const vector<const utf8*>& listCall, const utf8* pName )
    {
        for ( const utf8* pCall : listCall )
        {
            if ( StringUtil::equals( pCall, pName ) )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief [ModuleHostTest] 리로드 직전에 게임 상태를 찍지 못하면 게임을 내리지 않고 배치를 거절한다 — 옛 게임 모듈이 계속 돈다
 * @details 게임 상태는 새 이미지가 넘겨받을 유일한 것이다. 찍지 못한 채 내리면 게임 컴포넌트가 모든 씬에서 걷히고 되돌릴 스냅숏이 없다.
 *          거절(false)은 "아무것도 내리지 않았다" 는 계약이라 `LiveReloadManager` 는 새 이미지만 버린다. 찍으면 평소대로 내린다.
 */
SW_TEST_CASE( ModuleHostTest, ReloadBatchKeepsTheGameWhenItsStateCannotBeCaptured )
{
    RHI        rhi;
    ModuleHost host;
    SW_ASSERT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, false ) );
    int32 gameToken = 0;
    host.attachGameInstance( makeRecordingGameApi(), &gameToken );
    const vector<string> listBatch{ string{ "SWGame" } };

    s_listGameCall.clear();
    s_bGameSerializeSucceeds = false;
    SW_EXPECT_FALSE( host.onBeforeCommitBatch( listBatch ) );
    SW_EXPECT_TRUE( hasCall( s_listGameCall, "serializeState" ) );
    SW_EXPECT_FALSE_MSG( hasCall( s_listGameCall, "shutdown" ) || hasCall( s_listGameCall, "destroy" ),
                         "the game was torn down although its state was not captured" );

    s_listGameCall.clear();
    s_bGameSerializeSucceeds = true;
    SW_EXPECT_TRUE( host.onBeforeCommitBatch( listBatch ) );
    SW_EXPECT_TRUE( hasCall( s_listGameCall, "shutdown" ) );
    SW_EXPECT_TRUE( hasCall( s_listGameCall, "destroy" ) );

    host.shutdown();
}

/**
 * @brief [ModuleHostTest] 에디터와 게임 인스턴스를 같은 순서로 내린다 — 모듈 타입(과 그 컴포넌트)을 걷은 **뒤에** 서비스를 뗀다
 * @details 타입 등록 해제(`engine::unregisterModuleTypes`)는 그 모듈의 살아 있는 컴포넌트를 지운다. 소멸자는 모듈 코드라 서비스가 아직 붙어 있어야
 *          한다. 내리는 본문이 에디터 · 게임에 따로 적혀 있으면 한쪽만 순서가 바뀐다 — 각 모듈 이름으로 올린 탐침 변수가 서비스를 뗄 때 이미
 *          걷혔는지 본다.
 */
SW_TEST_CASE( ModuleHostTest, EditorAndGameTearDownInTheSameOrder )
{
    if ( engine::areEngineServicesBound() == false )
        SW_TEST_SKIP( "engine services are not bound in this executable" );

    GlobalVariableManager& variableManager = engine::getGlobalVariableManager();
    SW_ASSERT_TRUE( variableManager.registerVariable( kEditorTeardownProbe, GlobalVariableType::Int32, &s_teardownProbeValue, int32{ 0 },
                                                      "ModuleHostTest teardown probe", "", "EditorModule" ) );
    SW_ASSERT_TRUE( variableManager.registerVariable( kGameTeardownProbe, GlobalVariableType::Int32, &s_teardownProbeValue, int32{ 0 },
                                                      "ModuleHostTest teardown probe", "", "SWGame" ) );

    RHI        rhi;
    ModuleHost host;
    SW_ASSERT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, true ) );
    EditorAPI editorApi   = makeRecordingEditorApi();
    editorApi.bindService = &recordEditorBindService;
    GameAPI gameApi       = makeRecordingGameApi();
    gameApi.bindService   = &recordGameBindService;
    int32 editorToken     = 0;
    int32 gameToken       = 0;
    host.attachEditorInstance( editorApi, &editorToken );
    host.attachGameInstance( gameApi, &gameToken );

    s_bGameSerializeSucceeds   = true;
    s_editorUnbindCount        = 0;
    s_gameUnbindCount          = 0;
    s_bEditorTypesGoneAtUnbind = false;
    s_bGameTypesGoneAtUnbind   = false;
    host.suspendModules( ModuleScope::Both, true );

    SW_EXPECT_EQUAL( 1u, s_editorUnbindCount );
    SW_EXPECT_EQUAL( 1u, s_gameUnbindCount );
    SW_EXPECT_TRUE_MSG( s_bEditorTypesGoneAtUnbind, "the editor's services were unbound before its module types (and components) were released" );
    SW_EXPECT_TRUE_MSG( s_bGameTypesGoneAtUnbind, "the game's services were unbound before its module types (and components) were released" );

    // 뒤처리: 실패했을 때도 탐침이 다음 케이스로 새지 않게 한다. 저장 막음은 게임 내리기가 건 것이다.
    variableManager.unregisterVariablesByModule( "EditorModule" );
    variableManager.unregisterVariablesByModule( "SWGame" );
    engine::getSceneManager().setSaveBlockReason( {} );
    host.shutdown();
}

/**
 * @brief [ModuleHostTest] 새 이미지가 호스트의 API 표와 맞는지 옛 이미지를 내리기 전에 가린다
 * @details 같은 검사(`bindEditorApi` · `bindGameApi`)가 onAfterReload 에만 있으면 거절이 곧 에디터 · 게임을 잃는 일이다. 진짜 모듈은 받아들이고,
 *          표가 다른 모듈(에디터 자리에 게임 모듈)은 거절한다.
 */
SW_TEST_CASE( ModuleHostTest, ImageCheckAcceptsOnlyAModuleWithTheHostsApiTable )
{
    void* const pEditorModule = ModuleImageUtil::loadDynamicLibrary( ModuleImageUtil::findModuleLibraryPath( "EditorModule" ) );
    if ( pEditorModule == nullptr )
        SW_TEST_SKIP( "EditorModule is not built in Bin/Modules" );
    SW_TEST_DEFENSIVE_SCOPE( "a module with another API table is rejected and says why" );
    engine::registerModuleTypes( "EditorModule" ); // 정적 등록자를 전역 헤드에서 떼어 둔다(내릴 때 걷는다)

    RHI        rhi;
    ModuleHost host;
    SW_ASSERT_TRUE( host.initialize( nullptr, &rhi, nullptr, nullptr, false ) );
    SW_EXPECT_TRUE( host.isEditorImageUsable( pEditorModule ) );
    SW_EXPECT_FALSE( host.isGameImageUsable( pEditorModule ) );
    SW_EXPECT_FALSE( host.isEditorImageUsable( nullptr ) );
    host.shutdown();
    engine::unregisterModuleTypes( "EditorModule" );
    SW_EXPECT_TRUE( ModuleImageUtil::unloadModuleImage( "EditorModule", pEditorModule ) );
}
#endif

namespace
{
    /** @brief `ModuleHost::attachGameInstance` 가 이 구성에 있는가 — 가짜 API 표를 붙이는 시험 창구라 배포본에는 없어야 한다. */
    template <typename T, typename = void>
    struct HasAttachGameInstance : std::false_type
    {
    };
    template <typename T>
    struct HasAttachGameInstance<T, std::void_t<decltype( std::declval<T&>().attachGameInstance( std::declval<const GameAPI&>(), GameHandle{} ) )>>
        : std::true_type
    {
    };
    /** @brief `ModuleHost::attachEditorInstance` 가 이 구성에 있는가. */
    template <typename T, typename = void>
    struct HasAttachEditorInstance : std::false_type
    {
    };
    template <typename T>
    struct HasAttachEditorInstance<T, std::void_t<decltype( std::declval<T&>().attachEditorInstance( std::declval<const EditorAPI&>(), EditorHandle{} ) )>>
        : std::true_type
    {
    };
} // namespace

/**
 * @brief [ModuleHostTest] 가짜 API 표를 붙이는 시험 창구(`attachEditorInstance` · `attachGameInstance`)는 배포본에서 컴파일되지 않는다
 * @details 배포본에 남으면 호스트가 부르는 게임 API 표를 밖에서 바꿔 끼울 수 있다.
 */
SW_TEST_CASE( ModuleHostTest, AttachSeamsAreCompiledOutOfShipping )
{
#if defined( SW_SHIPPING )
    SW_EXPECT_FALSE( HasAttachGameInstance<ModuleHost>::value );
    SW_EXPECT_FALSE( HasAttachEditorInstance<ModuleHost>::value );
#else
    SW_EXPECT_TRUE( HasAttachGameInstance<ModuleHost>::value );
    SW_EXPECT_TRUE( HasAttachEditorInstance<ModuleHost>::value );
#endif
}
