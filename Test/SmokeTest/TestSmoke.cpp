#include "pch.h"

#include "App/Module/LiveReloadManager.h"
#include "App/Module/ModuleCompiler.h"
#include "App/Module/ModuleImagePatch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/Modules/RHIModuleAbi.h"
#include "Engine/Module/EngineAbiStamp.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"

#include "GameFramework/Base/GameService.h"
#include "GameFramework/GameFrameworkExports.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/ABI/GameAPI.h"

#include "TestFramework/TestFramework.h"

#if !defined( SW_SHIPPING )

namespace sw
{
    namespace
    {
        /** @brief 테스트 모듈 DLL 경로를 만듭니다. */
        sw::string modulePath( const utf8* pBaseName )
        {
    #if defined( SW_TEST_MODULE_DIR )
            const sw::string dir = SW_TEST_MODULE_DIR;
    #else
            const sw::string dir = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
    #endif
            return dir + "/" + sw::FileUtil::formatSharedLibraryName( pBaseName );
        }

        /** @brief 테스트 모듈을 동적 로드합니다. */
        void* loadModule( const utf8* pName )
        {
            const sw::string path = modulePath( pName );
            return sw::FileUtil::loadDynamicLibrary( path );
        }

        /**
         * @brief SWGame 의 게임 인스턴스를 한 번 만들고 부숩니다.
         * @details 만들 때 GameFramework 의 코드(게임 인스턴스 바탕 클래스)가 돌아서, Windows 에서는 SWGame 의 GameFramework
         *          지연 로드가 이 자리에서 풀립니다. 결속 확인이 "아직 안 풀림" 이 아니라 실제로 묶인 이미지를 보게 하려는 것입니다.
         */
        bool createAndDestroyGame( void* pGameModule )
        {
            if ( pGameModule == nullptr )
                return false;
            const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::FileUtil::getDynamicSymbol( pGameModule, "exportGameApi" ) );
            if ( pfnExport == nullptr )
                return false;
            sw::GameAPI api{};
            if ( pfnExport( &api ) == false || api.create == nullptr || api.destroy == nullptr )
                return false;

            sw::ModuleService gameService{};
            if ( api.bindService != nullptr )
            {
                sw::engine::fillModuleServices( gameService, true );
                api.bindService( &gameService );
            }
            sw::GameHandle game     = api.create();
            const bool     bCreated = game != nullptr;
            if ( bCreated )
                api.destroy( game );
            if ( api.bindService != nullptr )
                api.bindService( nullptr );
            return bCreated;
        }

        /** @brief 모듈 하나를 강제로 리로드하고 onAfter 가 불릴 때까지 기다립니다. 기다리다 넘치면 false 입니다. */
        bool reloadAndWait( sw::LiveReloadManager& manager, const utf8* pModuleName )
        {
            bool bReloaded{ false };
            manager.setOnAfterReload(
                pModuleName,
                SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&bReloaded]( void* )
            {
                bReloaded = true;
            } ) );
            manager.triggerReload( pModuleName );
            for ( int32 stepIndex = 0; stepIndex < 100 && bReloaded == false; ++stepIndex )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
                manager.update();
            }
            manager.setOnAfterReload( pModuleName, sw::LiveReloadManager::OnAfterReloadDelegate{} );
            return bReloaded;
        }

        /** @brief 처리기만 들고 아무것도 띄우지 않는 창입니다. 모듈 코드 정리 테스트가 활성 창으로 씁니다. */
        class HandlerOnlyWindow final : public sw::IWindow
        {
        public:
            bool  initializeWindow( const utf8*, uint32, uint32 ) override { return true; }
            void  destroy() override {}
            bool  processMessages() override { return true; }
            void* getNativeHandle() const override { return nullptr; }
        };

        // 모듈 코드 정리 테스트가 등록부마다 하나씩 다는 함수들이다. 몸통을 서로 다르게 둔다 — 같으면 링커가 접어 스텁 주소가 겹칠 수 있다.
        int32 s_sweepProbeValue{ 0 };

        /** @brief 이벤트 버스에 구독하는 처리기입니다. */
        void onSweepProbeResize( const sw::WindowResizeEvent& )
        {
            s_sweepProbeValue += 1;
        }

        /** @brief 전역 로그 리스너입니다. */
        void onSweepProbeLog( const sw::LogEntry& )
        {
            s_sweepProbeValue += 20;
        }

        /** @brief Undo 명령의 redo 입니다. */
        void redoSweepProbe()
        {
            s_sweepProbeValue += 300;
        }

        /** @brief Undo 명령의 undo 입니다. */
        void undoSweepProbe()
        {
            s_sweepProbeValue -= 300;
        }

        /** @brief 창 닫기 처리기입니다. 닫기를 보류합니다. */
        bool refuseSweepProbeClose()
        {
            s_sweepProbeValue += 4000;
            return false;
        }

        // 컴파일러가 널임을 증명하지 못하게 전역에 둔다(증명하면 쓰기를 트랩 명령으로 바꾼다).
        uintptr_t s_reloadFaultAddress{ 0 };

        /** @brief 델리게이트의 스텁 하나만 담는 범위로 `engine::releaseModuleCode` 를 부릅니다. */
        template <typename TDelegate>
        uint32 releaseStubOf( const TDelegate& delegate )
        {
            const uint8* pCode = static_cast<const uint8*>( delegate.getCodeAddress() );
            return sw::engine::releaseModuleCode( "SweepProbe", pCode, pCode + 1 );
        }
    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) Architecture — RHI ABI·핫리로드 keep-old
// ------------------------------------------------------------------------------
/**
 * @brief [ArchitectureTest] 모든 RHI 백엔드 모듈 (DX11, DX12, Vulkan, GL) ABI 스탬프 및 팩토리 export 검증
 */
SW_TEST_CASE( ArchitectureTest, AllRHIModulesAbiStampExports )
{
    const utf8* kRhiModules[] = { "RHI_DX11", "RHI_DX12", "RHI_Vulkan", "RHI_GL" };

    for ( const utf8* modName : kRhiModules )
    {
        const sw::string path = sw::modulePath( modName );
        if ( sw::FileUtil::fileExists( path ) == false )
            continue;

        void* handle = sw::FileUtil::loadDynamicLibrary( path );
        SW_EXPECT_TRUE( handle != nullptr );
        if ( handle == nullptr )
            continue;

        const sw::PFN_GetRHIModuleAbiVersion pfnVersion = reinterpret_cast<sw::PFN_GetRHIModuleAbiVersion>(
            sw::FileUtil::getDynamicSymbol( handle, "getRHIModuleAbiVersion" ) );
        const sw::PFN_GetRHIModuleAbiStamp pfnStamp = reinterpret_cast<sw::PFN_GetRHIModuleAbiStamp>(
            sw::FileUtil::getDynamicSymbol( handle, "getRHIModuleAbiStamp" ) );
        const void* pfnCreate = reinterpret_cast<void*>(
            sw::FileUtil::getDynamicSymbol( handle, "createRHIDevice" ) );

        SW_EXPECT_TRUE( pfnVersion != nullptr );
        SW_EXPECT_TRUE( pfnStamp != nullptr );
        SW_EXPECT_TRUE( pfnCreate != nullptr );

        if ( pfnVersion != nullptr )
            SW_EXPECT_EQUAL( sw::kRHIModuleAbiVersion, pfnVersion() );
        if ( pfnStamp != nullptr && pfnStamp() != nullptr )
            SW_EXPECT_STREQ( sw::kRHIModuleAbiStamp, pfnStamp() );

        sw::FileUtil::unloadDynamicLibrary( handle );
    }
}

/**
 * @brief [ArchitectureTest] 원본 없으면 LiveReload 가 이전 모듈을 유지
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadKeepOldOnMissingOriginal )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing missing DLL module handling" );
    sw::LiveReloadManager manager;
    SW_EXPECT_TRUE( manager.registerModule( "SWGame" ) );
    void* before = manager.getModuleHandle( "SWGame" );
    SW_EXPECT_TRUE( before != nullptr );
    if ( before == nullptr )
        return;

    // 가짜 이름으로 다시 등록해 원본을 존재하지 않는 경로로 가리키면 실패한다.
    // 없는 파일에 registerModule 하면 false 를 반환하고 다른 모듈은 지우지 않는다.
    SW_EXPECT_FALSE( manager.registerModule( "DefinitelyMissingModule_ZZZ" ) );
    void* after = manager.getModuleHandle( "SWGame" );
    SW_EXPECT_EQUAL( before, after );
    manager.shutdown();
}

/**
 * @brief poison 된 LiveReload 그래프는 이후 triggerReload 를 무시한다
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadPoisonIgnoresTrigger )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing broken/poisoned reload graph handling" );
    sw::LiveReloadManager manager;
    SW_EXPECT_FALSE( manager.isGraphBroken() );
    manager.markGraphBroken( "test" );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
    manager.triggerReload( "SWGame" );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
}

/**
 * @brief onAfter poison 시 registerModule 은 실패해야 한다
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadOnAfterPoisonFailsRegister )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing onAfter poison registration failure" );
    sw::LiveReloadManager manager;
    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&manager]( void* )
    {
        manager.markGraphBroken( "test onAfter" );
    } ) );
    SW_EXPECT_FALSE( manager.registerModule( "SWGame" ) );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
    manager.shutdown();
}

/**
 * @brief 캐스케이드 중 앞 모듈 onAfter poison 이면 이후 모듈은 commit 하지 않는다
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadCascadeAbortsAfterOnAfterPoison )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing cascade abort on poisoned dependent module" );
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::fileExists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::LiveReloadManager manager;
    SW_EXPECT_TRUE( manager.registerModule( "GameFramework" ) );

    sw::vector<sw::string> gameDepends;
    gameDepends.push_back( "GameFramework" );
    if ( manager.registerModule( "SWGame", gameDepends ) == false )
        SW_TEST_SKIP( "SWGame MODULE not available for cascade test" );

    void* const gameBefore = manager.getModuleHandle( "SWGame" );
    SW_EXPECT_TRUE( gameBefore != nullptr );

    manager.setOnAfterReload(
        "GameFramework",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&manager]( void* )
    {
        manager.markGraphBroken( "test cascade onAfter" );
    } ) );
    manager.triggerReload( "GameFramework" );

    bool broken{ false };
    for ( int32 stepIndex = 0; stepIndex < 40 && broken == false; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
        manager.update();
        broken = manager.isGraphBroken();
    }
    SW_EXPECT_TRUE( broken );
    SW_EXPECT_EQUAL( gameBefore, manager.getModuleHandle( "SWGame" ) );
    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 단일 모듈 LiveReload 정상 성공 및 섀도 핸들 갱신 검증 (Happy Path)
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadSuccessfulShadowReload )
{
    sw::LiveReloadManager manager;
    if ( manager.registerModule( "SWGame" ) == false )
        SW_TEST_SKIP( "SWGame MODULE not available for LiveReload test" );

    void* const initialHandle = manager.getModuleHandle( "SWGame" );
    SW_EXPECT_TRUE( initialHandle != nullptr );

    bool  onBeforeCalled{ false };
    bool  onAfterCalled{ false };
    void* newHandleInCb{ nullptr };

    manager.setOnBeforeReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeReloadDelegate, [&onBeforeCalled]()
    {
        onBeforeCalled = true;
    } ) );

    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled, &newHandleInCb]( void* pH )
    {
        onAfterCalled = true;
        newHandleInCb = pH;
    } ) );

    // 리로드 트리거
    manager.triggerReload( "SWGame" );

    // 업데이트 루프로 리로드 완료 대기 (디바운스 300ms 초과 대기)
    for ( int32 stepIndex = 0; stepIndex < 80; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( onAfterCalled )
            break;
    }

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( onBeforeCalled );
    SW_EXPECT_TRUE( onAfterCalled );
    SW_EXPECT_TRUE( newHandleInCb != nullptr );

    void* const finalHandle = manager.getModuleHandle( "SWGame" );
    SW_EXPECT_EQUAL( newHandleInCb, finalHandle );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 모듈 리플렉션 registrar 생명주기 — 등록/리로드/언로드 시 TypeRegistry
 *        내용과 전역 registrar 헤드 상태를 고정한다(#4 안전망).
 * @details registerModuleTypes 는 매 로드 후 전역 TypeRegistrar::getHead() 를 nullptr 로 drain 한다.
 *          그 불변식과 "리로드해도 타입 수 불변 / 언로드하면 원복" 을 명시 검증한다.
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadRegistrarContentLifecycle )
{
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::fileExists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    const auto collectFqn = [&registry]()
    {
        sw::vector<sw::hashed_string> listFqn;
        registry.forEachType( [&listFqn]( const sw::TypeInfo& info )
        {
            listFqn.push_back( info._fullyQualifiedName );
        } );
        std::sort( listFqn.begin(), listFqn.end() );
        return listFqn;
    };

    const sw::vector<sw::hashed_string> baseTypes = collectFqn();
    SW_EXPECT_EQUAL( static_cast<sw::TypeRegistrar*>( nullptr ), sw::TypeRegistrar::getHead() );
    SW_EXPECT_EQUAL( static_cast<sw::EnumRegistrar*>( nullptr ), sw::EnumRegistrar::getHead() );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GameFramework" ) == false )
        SW_TEST_SKIP( "GameFramework MODULE register failed in this config" );

    const sw::vector<sw::hashed_string> afterRegister = collectFqn();
    SW_EXPECT_TRUE( afterRegister.size() > baseTypes.size() );
    SW_EXPECT_EQUAL( static_cast<sw::TypeRegistrar*>( nullptr ), sw::TypeRegistrar::getHead() );

    // 모듈이 새로 들여온 타입 하나를 대표로 잡는다.
    sw::hashed_string sampleFqn{};
    bool              bHasSample{ false };
    for ( const sw::hashed_string& fqn : afterRegister )
    {
        if ( std::binary_search( baseTypes.begin(), baseTypes.end(), fqn ) == false )
        {
            sampleFqn  = fqn;
            bHasSample = true;
            break;
        }
    }
    SW_EXPECT_TRUE( bHasSample );
    SW_EXPECT_TRUE( registry.findType( sampleFqn ) != nullptr );

    // 1) 성공 리로드: 타입 수 불변, 대표 타입 유지, 전역 헤드 drain 유지
    bool onAfterCalled{ false };
    manager.setOnAfterReload(
        "GameFramework",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled]( void* )
    {
        onAfterCalled = true;
    } ) );
    manager.triggerReload( "GameFramework" );
    for ( int32 stepIndex = 0; stepIndex < 80 && onAfterCalled == false; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
    }
    SW_EXPECT_TRUE( onAfterCalled );
    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_EQUAL( afterRegister.size(), collectFqn().size() );
    SW_EXPECT_TRUE( registry.findType( sampleFqn ) != nullptr );
    SW_EXPECT_EQUAL( static_cast<sw::TypeRegistrar*>( nullptr ), sw::TypeRegistrar::getHead() );

    // 2) 언로드: 타입 수 원복, 대표 타입 제거, 전역 헤드 drain 유지
    manager.shutdown();
    SW_EXPECT_EQUAL( baseTypes.size(), collectFqn().size() );
    SW_EXPECT_TRUE( registry.findType( sampleFqn ) == nullptr );
    SW_EXPECT_EQUAL( static_cast<sw::TypeRegistrar*>( nullptr ), sw::TypeRegistrar::getHead() );
}

/**
 * @brief [ArchitectureTest] 종속 모듈 간 캐스케이드 LiveReload 순차 성공 검증 (GameFramework -> SWGame)
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadCascadeSuccessPath )
{
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::fileExists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GameFramework" ) == false )
        SW_TEST_SKIP( "GameFramework MODULE registration failed" );

    sw::vector<sw::string> gameDepends;
    gameDepends.push_back( "GameFramework" );
    if ( manager.registerModule( "SWGame", gameDepends ) == false )
        SW_TEST_SKIP( "SWGame MODULE registration failed" );

    sw::vector<sw::string> reloadLog;

    manager.setOnBeforeReload(
        "GameFramework",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeReloadDelegate, [&reloadLog]()
    {
        reloadLog.push_back( "GF_Before" );
    } ) );

    manager.setOnBeforeReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeReloadDelegate, [&reloadLog]()
    {
        reloadLog.push_back( "Game_Before" );
    } ) );

    manager.setOnAfterReload(
        "GameFramework",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&reloadLog]( void* )
    {
        reloadLog.push_back( "GF_After" );
    } ) );

    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&reloadLog]( void* )
    {
        reloadLog.push_back( "Game_After" );
    } ) );

    // GameFramework 리로드 시 종속된 SWGame까지 캐스케이드 리로드되어야 함
    manager.triggerReload( "GameFramework" );

    for ( int32 stepIndex = 0; stepIndex < 80; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( reloadLog.size() >= 4u )
            break;
    }

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( reloadLog.size() >= 4u );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 모듈 이미지를 내리기 전의 정리는 에디터가 다는 엔진 쪽 등록부 넷을 모두 본다
 * @details `engine::releaseModuleCode` 는 모듈이 스스로 떼지 않은 것을 떼는 안전망이다. 에디터는 이벤트 구독 · 로그 리스너(콘솔 패널) ·
 *          Undo 명령(트랜잭션) · 창 닫기 처리기를 달고, 지금은 `ImGuiEditor::shutdown` · `~ConsolePanel` 이 손으로 뗀다. 여기서는 등록부마다
 *          하나씩 달고 범위를 그 하나의 스텁으로 좁혀 부른다 — 이 실행 파일의 다른 등록은 건드리지 않고, 등록부 하나를 빠뜨리면 진다.
 */
SW_TEST_CASE( ArchitectureTest, ReleaseModuleCodeSweepsEveryRegistryTheEditorUses )
{
    if ( sw::engine::areEngineServicesBound() == false )
        SW_TEST_SKIP( "engine services not bound" );
    SW_TEST_DEFENSIVE_SCOPE( "releaseModuleCode warns about what a module left behind" );
    sw::s_sweepProbeValue = 0;

    // 이벤트 버스
    using ResizeDelegate          = sw::Delegate<void( const sw::WindowResizeEvent& )>;
    const ResizeDelegate onResize = SW_DELEGATE_FUNCTION( ResizeDelegate, sw::onSweepProbeResize );
    sw::engine::getEventDispatcher().subscribe<sw::WindowResizeEvent>( onResize );
    SW_EXPECT_EQUAL( 1u, sw::releaseStubOf( onResize ) );
    sw::WindowResizeEvent resize;
    resize._width  = 1;
    resize._height = 1;
    sw::engine::getEventDispatcher().publish( resize );

    // 전역 로그 리스너
    const sw::LogWrittenDelegate onLog = SW_DELEGATE_FUNCTION( sw::LogWrittenDelegate, sw::onSweepProbeLog );
    SW_ASSERT_TRUE( sw::Logger::addGlobalListener( onLog ).isValid() );
    SW_EXPECT_EQUAL( 1u, sw::releaseStubOf( onLog ) );
    SW_EXPECT_EQUAL( 0u, sw::releaseStubOf( onLog ) );

    // Undo 스택
    sw::CommandStack* pCommandStack = sw::engine::getBoundEngineServices()._pCommandStack;
    SW_ASSERT_TRUE( pCommandStack != nullptr );
    pCommandStack->clear();
    sw::CommandStack::Command command;
    command._label                       = "sweep probe";
    command._redo                        = SW_DELEGATE_FUNCTION( sw::Delegate<void()>, sw::redoSweepProbe );
    command._undo                        = SW_DELEGATE_FUNCTION( sw::Delegate<void()>, sw::undoSweepProbe );
    const sw::Delegate<void()> undoProbe = command._undo;
    pCommandStack->push( std::move( command ) );
    SW_EXPECT_EQUAL( 1u, sw::releaseStubOf( undoProbe ) );
    SW_EXPECT_FALSE( pCommandStack->canUndo() );

    // 활성 창의 닫기 처리기
    sw::HandlerOnlyWindow window;
    sw::IWindow* const    pPreviousWindow = sw::IWindow::getActiveWindow();
    sw::IWindow::setActiveWindow( &window );
    const sw::WindowCloseQueryDelegate onClose = SW_DELEGATE_FUNCTION( sw::WindowCloseQueryDelegate, sw::refuseSweepProbeClose );
    window.setCloseQueryHandler( onClose );
    SW_EXPECT_FALSE( window.tryBeginClose() );
    SW_EXPECT_EQUAL( 1u, sw::releaseStubOf( onClose ) );
    SW_EXPECT_TRUE( window.tryBeginClose() );
    sw::IWindow::setActiveWindow( pPreviousWindow );

    // 뗀 뒤에 불린 것은 없다 — 닫기 처리기가 떼기 전에 한 번 불렸을 뿐이다.
    SW_EXPECT_EQUAL( 4000, sw::s_sweepProbeValue );
}

/**
 * @brief [ArchitectureTest] 새 모듈이 onAfterReload 안에서 결함을 내면 프로세스가 아니라 그 모듈이 멈춘다
 * @details onAfterReload 는 새 이미지의 코드가 처음 도는 자리다(ModuleHost 는 여기서 API 를 바인딩하고 인스턴스를 만든다). 거기서
 *          접근 위반이 나면 예전에는 에디터째 내려갔다. 이제는 그래프를 막고 결함 콜백을 부른다 — ModuleHost 는 그 콜백에서 받은 것을
 *          모듈을 부르지 않고 버린다. 여기서는 콜백 자리에 일부러 널 쓰기를 둔다.
 */
SW_TEST_CASE( ArchitectureTest, FaultInOnAfterReloadStopsTheModuleNotTheProcess )
{
    const sw::string gamePath = sw::modulePath( "SWGame" );
    if ( sw::FileUtil::fileExists( gamePath ) == false )
        SW_TEST_SKIP( "SWGame MODULE not built in this config" );
    SW_TEST_DEFENSIVE_SCOPE( "a fault in onAfterReload is contained" );

    sw::LiveReloadManager manager;
    SW_ASSERT_TRUE( manager.registerModule( "SWGame" ) );

    bool   bFaultReported{ false };
    uint32 reportedFaultCode{ 0 };
    manager.setOnReloadFault( "SWGame", SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnReloadFaultDelegate, [&bFaultReported, &reportedFaultCode]( uint32 faultCode )
    {
        bFaultReported    = true;
        reportedFaultCode = faultCode;
    } ) );
    manager.setOnAfterReload( "SWGame", SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, []( void* )
    {
        *reinterpret_cast<volatile int32*>( sw::s_reloadFaultAddress ) = 7;
    } ) );

    manager.triggerReload( "SWGame" );
    for ( int32 stepIndex = 0; stepIndex < 100 && bFaultReported == false; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
    }

    SW_EXPECT_TRUE( bFaultReported );
    SW_EXPECT_TRUE( reportedFaultCode != 0 );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] EditorModule DLL 독립 LiveReload 및 C-ABI 테이블 재바인딩 검증
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadEditorModule )
{
    const sw::string editorPath = sw::modulePath( "EditorModule" );
    if ( sw::FileUtil::fileExists( editorPath ) == false )
        SW_TEST_SKIP( "EditorModule not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "EditorModule" ) == false )
        SW_TEST_SKIP( "EditorModule registration failed" );

    void* const initialHandle = manager.getModuleHandle( "EditorModule" );
    SW_ASSERT_NOT_NULL( initialHandle );

    bool  onBeforeCalled{ false };
    bool  onAfterCalled{ false };
    void* newHandle{ nullptr };

    manager.setOnBeforeReload(
        "EditorModule",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeReloadDelegate, [&onBeforeCalled]()
    {
        onBeforeCalled = true;
    } ) );

    manager.setOnAfterReload(
        "EditorModule",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled, &newHandle]( void* pH )
    {
        onAfterCalled = true;
        newHandle     = pH;
    } ) );

    manager.triggerReload( "EditorModule" );

    for ( int32 stepIndex = 0; stepIndex < 80; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( onAfterCalled )
            break;
    }

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( onBeforeCalled );
    SW_EXPECT_TRUE( onAfterCalled );
    SW_ASSERT_NOT_NULL( newHandle );

    // 새로 로드된 모듈에서 C-ABI exportEditorApi 정상 동작 검증
    const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
        sw::FileUtil::getDynamicSymbol( newHandle, "exportEditorApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );

    sw::EditorAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );
    SW_EXPECT_TRUE( api.create != nullptr );
    SW_EXPECT_TRUE( api.render != nullptr );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 장르 키트 모듈 (GF_Overworld, GF_TurnBattle, GF_ActionCombat) 개별 LiveReload 검증
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadGenreKitsIndividuallyAndCascaded )
{
    const utf8* kKits[] = { "GF_Overworld", "GF_TurnBattle", "GF_ActionCombat" };

    for ( const utf8* kitName : kKits )
    {
        const sw::string kitPath = sw::modulePath( kitName );
        if ( sw::FileUtil::fileExists( kitPath ) == false )
            continue;

        sw::LiveReloadManager manager;
        if ( manager.registerModule( kitName ) == false )
            continue;

        bool  reloaded{ false };
        void* newH{ nullptr };

        manager.setOnAfterReload(
            kitName,
            SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&reloaded, &newH]( void* pH )
        {
            reloaded = true;
            newH     = pH;
        } ) );

        manager.triggerReload( kitName );

        for ( int32 stepIndex = 0; stepIndex < 80; ++stepIndex )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
            manager.update();
            if ( reloaded )
                break;
        }

        SW_EXPECT_FALSE( manager.isGraphBroken() );
        SW_EXPECT_TRUE( reloaded );
        SW_EXPECT_TRUE( newH != nullptr );

        manager.shutdown();
    }
}

/**
 * @brief [ArchitectureTest] 풀스택 복합 의존 그래프 (GameFramework + GF_Overworld + SWGame + EditorModule) 동시 및 캐스케이드 LiveReload
 */
SW_TEST_CASE( ArchitectureTest, MultiModuleFullStackLiveReload )
{
    sw::LiveReloadManager manager;

    sw::vector<sw::string> gameDepends;
    gameDepends.push_back( "GF_Overworld" );
    if ( manager.registerModule( "GF_Overworld" ) == false )
        SW_TEST_SKIP( "GF_Overworld registration failed" );

    if ( manager.registerModule( "SWGame", gameDepends ) == false )
        SW_TEST_SKIP( "SWGame registration failed" );

    if ( manager.registerModule( "EditorModule" ) == false )
        SW_TEST_SKIP( "EditorModule registration failed" );

    // 1) GF_Overworld 핫리로드 트리거 -> 종속된 SWGame까지 캐스케이드 리로드
    bool kitReloaded{ false };
    bool gameReloaded{ false };

    manager.setOnAfterReload(
        "GF_Overworld",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&kitReloaded]( void* )
    {
        kitReloaded = true;
    } ) );

    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&gameReloaded]( void* )
    {
        gameReloaded = true;
    } ) );

    manager.triggerReload( "GF_Overworld" );

    for ( int32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( kitReloaded && gameReloaded )
            break;
    }

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( kitReloaded );
    SW_EXPECT_TRUE( gameReloaded );

    // 2) 독립적인 EditorModule 리로드 트리거
    bool editorReloaded{ false };
    manager.setOnAfterReload(
        "EditorModule",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&editorReloaded]( void* )
    {
        editorReloaded = true;
    } ) );

    manager.triggerReload( "EditorModule" );

    for ( int32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( editorReloaded )
            break;
    }

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( editorReloaded );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] App 과 같은 사슬(GameFramework → 킷 → SWGame)을 연쇄 리로드해도, 의존 모듈은 **지금의** 복사본에 묶인다
 * @details 섀도 복사본은 파일 이름이 원본과 달라서 의존 모듈이 어느 이미지에 묶일지를 로더가 정한다. Windows 는 지연 로드 훅이
 *          `LiveReloadManager` 에게 물어 지금의 복사본을 받고, 리눅스는 SONAME 이 같은 **먼저 올라온** 이미지가 이긴다. 어긋나면
 *          GameFramework 가 한 프로세스에 두 벌 돌고, 옛 복사본을 내리는 순간 그리로 뛰는 코드가 죽는다. 예전 테스트들은 리로드가
 *          "끝났다" 만 봤지 누가 누구에게 묶였는지는 보지 않았다.
 *
 *          Windows 에서는 SWGame 이 GameFramework 를 실제로 불러야 지연 로드가 풀리므로, 리로드 앞뒤로 게임 인스턴스를 한 번씩
 *          만들고 부순 뒤에 확인한다. (킷의 GameFramework 지연 로드는 킷 코드가 돌기 전까지 풀리지 않을 수 있다 — 그 경우 확인은
 *          "아직 안 묶임" 으로 지나간다. 리눅스는 `RTLD_NOW` 라 로드하는 순간 모두 묶인다.)
 */
SW_TEST_CASE( ArchitectureTest, ReloadedDependentsBindToTheCurrentImages )
{
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::fileExists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GameFramework" ) == false )
        SW_TEST_SKIP( "GameFramework registration failed" );
    if ( manager.registerModule( "GF_Overworld", { "GameFramework" } ) == false )
        SW_TEST_SKIP( "GF_Overworld registration failed" );
    if ( manager.registerModule( "SWGame", { "GameFramework", "GF_Overworld" } ) == false )
        SW_TEST_SKIP( "SWGame registration failed" );

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( sw::createAndDestroyGame( manager.getModuleHandle( "SWGame" ) ) );
    SW_EXPECT_TRUE( manager.verifyModuleBindings() );

    bool bGameReloaded{ false };
    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&bGameReloaded]( void* )
    {
        bGameReloaded = true;
    } ) );

    // GameFramework 를 바꾸면 킷과 SWGame 까지 연쇄로 바뀐다(App 에서 GameFramework 를 고치고 빌드한 경우).
    manager.triggerReload( "GameFramework" );
    for ( int32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( bGameReloaded )
            break;
    }

    SW_EXPECT_TRUE( bGameReloaded );
    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( sw::createAndDestroyGame( manager.getModuleHandle( "SWGame" ) ) );
    SW_EXPECT_TRUE( manager.verifyModuleBindings() );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 교체된 옛 이미지는 바로 내려가지 않고, 배치가 상한을 넘을 때 오래된 것부터 내려간다
 * @details 옛 코드를 가리키는 것이 남아 있어도 이미지가 올라와 있는 동안은 크래시가 아니라 옛 동작이 한 번 더 돈다. 그래서 첫 이미지에서
 *          얻은 함수 포인터를 리로드 **뒤에** 불러 본다 — 예전(바로 `FreeLibrary`)에는 이 호출이 내려간 코드로 뛰었다. 상한보다 많이
 *          리로드하면 퇴역 수는 상한에서 멈추고, 종료하면 모두 내려간다.
 */
SW_TEST_CASE( ArchitectureTest, RetiredImagesStayMappedUntilTheirBatchIsEvicted )
{
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::fileExists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GameFramework" ) == false )
        SW_TEST_SKIP( "GameFramework registration failed" );
    if ( manager.registerModule( "SWGame", { "GameFramework" } ) == false )
        SW_TEST_SKIP( "SWGame registration failed" );
    SW_EXPECT_EQUAL( 0u, manager.getRetiredImageCount() );

    void* const                 pFirstGame     = manager.getModuleHandle( "SWGame" );
    const sw::PFN_ExportGameAPI pfnFirstExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::FileUtil::getDynamicSymbol( pFirstGame, "exportGameApi" ) );
    SW_ASSERT_TRUE( pfnFirstExport != nullptr );

    SW_ASSERT_TRUE( sw::reloadAndWait( manager, "SWGame" ) );
    SW_EXPECT_TRUE( manager.getModuleHandle( "SWGame" ) != pFirstGame );
    SW_EXPECT_EQUAL( 1u, manager.getRetiredImageCount() );

    // 첫 이미지는 아직 올라와 있다 — 그 코드를 불러도 된다.
    sw::GameAPI firstApi{};
    SW_EXPECT_TRUE( pfnFirstExport( &firstApi ) );

    for ( uint32 reloadIndex = 0; reloadIndex < sw::LiveReloadManager::kMaxRetiredBatchCount + 2; ++reloadIndex )
    {
        SW_ASSERT_TRUE( sw::reloadAndWait( manager, "SWGame" ) );
    }
    // SWGame 만 바뀌는 연쇄는 배치 하나에 이미지 하나다. 상한에서 멈춘다.
    SW_EXPECT_EQUAL( sw::LiveReloadManager::kMaxRetiredBatchCount, manager.getRetiredImageCount() );
    SW_EXPECT_FALSE( manager.isGraphBroken() );

    manager.shutdown();
    SW_EXPECT_EQUAL( 0u, manager.getRetiredImageCount() );
}

/**
 * @brief [ArchitectureTest] 빌드된 모듈은 돌고 있는 엔진과 같은 ABI 도장을 들고 있다
 * @details 도장은 Core · Engine 헤더 내용의 지문이고 Engine 과 모듈이 같은 생성 헤더로 굽는다. 이 테스트는 빌드 연결(생성 소스가 모듈마다
 *          들어가고, 링커가 지우지 않는다)을 실제 파일로 확인한다.
 */
SW_TEST_CASE( ArchitectureTest, ModulesCarryTheRunningEngineAbiStamp )
{
    // 에디터도 같은 매니저로 리로드된다 — 도장이 빠지면 에디터만 헤더가 어긋난 채 올라간다.
    const utf8* arrModuleName[] = { "GameFramework", "SWGame", "EditorModule" };
    for ( const utf8* pModuleName : arrModuleName )
    {
        const sw::string path = sw::modulePath( pModuleName );
        if ( sw::FileUtil::fileExists( path ) == false )
            SW_TEST_SKIP( "module not built in this config" );

        sw::vector<uint8> bytes;
        SW_ASSERT_TRUE( sw::FileUtil::readFile( path, bytes ) );
        sw::string stamp;
        SW_ASSERT_TRUE( sw::ModuleImagePatch::findEngineAbiStamp( bytes, stamp ) );
        SW_EXPECT_STREQ( sw::engine::getEngineAbiStamp(), stamp.c_str() );
    }
}

/**
 * @brief [ArchitectureTest] 다른 엔진 헤더로 빌드된 모듈은 올리기 전에 거절되고, 그래프는 막히지 않는다
 * @details SWGame 을 다른 이름으로 복사해 도장 한 글자를 바꾼다(엔진 헤더를 고친 빌드에서 Engine.dll 은 잠겨 못 바뀌고 모듈만 새로 써진
 *          경우). 등록은 실패해야 하고 — 모듈 코드는 한 줄도 돌지 않았다 — 옛 모듈을 유지하는 실패라 그래프는 멀쩡해야 한다.
 */
SW_TEST_CASE( ArchitectureTest, ModuleBuiltAgainstOtherEngineHeadersIsRejected )
{
    const sw::string gamePath = sw::modulePath( "SWGame" );
    if ( sw::FileUtil::fileExists( gamePath ) == false )
        SW_TEST_SKIP( "SWGame MODULE not built in this config" );
    SW_TEST_DEFENSIVE_SCOPE( "a module built against other engine headers is rejected before it loads" );

    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( gamePath, bytes ) );
    sw::string stamp;
    SW_ASSERT_TRUE( sw::ModuleImagePatch::findEngineAbiStamp( bytes, stamp ) );

    // 도장의 마지막 16진 글자를 바꾼다. 같은 길이라 파일 배치는 그대로다.
    const sw::string_view view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() };
    const size_t          stampPos = view.find( stamp );
    SW_ASSERT_TRUE( stampPos != sw::string_view::npos );
    uint8& lastDigit = bytes[stampPos + stamp.size() - 1];
    lastDigit        = ( lastDigit == '0' ) ? '1' : '0';

    const sw::string probePath = sw::modulePath( "SWGameAbiProbe" );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( probePath, bytes.data(), bytes.size() ) );

    sw::LiveReloadManager manager;
    SW_EXPECT_FALSE( manager.registerModule( "SWGameAbiProbe" ) );
    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( manager.getModuleHandle( "SWGameAbiProbe" ) == nullptr );
    manager.shutdown();

    sw::FileUtil::removeFile( probePath );
}

/**
 * @brief [ArchitectureTest] ModuleCompiler (CMake 백그라운드 컴파일) -> LiveReloadManager (DLL 핫스왑) End-to-End 전체 파이프라인 검증
 */
SW_TEST_CASE( ArchitectureTest, ModuleCompilerAndLiveReloadE2E )
{
    #if defined( SW_SHIPPING )
    SW_TEST_SKIP( "ModuleCompiler is only supported in Dev / non-shipping builds" );
    #else
    const sw::string editorPath = sw::modulePath( "EditorModule" );
    if ( sw::FileUtil::fileExists( editorPath ) == false )
        SW_TEST_SKIP( "EditorModule not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "EditorModule" ) == false )
        SW_TEST_SKIP( "EditorModule registration failed" );

    void* const initialHandle = manager.getModuleHandle( "EditorModule" );
    SW_ASSERT_NOT_NULL( initialHandle );

    bool  onBeforeCalled{ false };
    bool  onAfterCalled{ false };
    void* newHandle{ nullptr };

    manager.setOnBeforeReload(
        "EditorModule",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeReloadDelegate, [&onBeforeCalled]()
    {
        onBeforeCalled = true;
    } ) );

    manager.setOnAfterReload(
        "EditorModule",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled, &newHandle]( void* pH )
    {
        onAfterCalled = true;
        newHandle     = pH;
    } ) );

    sw::ModuleCompiler compiler{ &manager };

    // 1) 초기 상태 머신 검증
    SW_EXPECT_EQUAL( static_cast<int32>( sw::BuildState::Idle ), static_cast<int32>( compiler.getBuildState() ) );
    SW_EXPECT_FALSE( compiler.isCompiling() );

    // 2) EditorModule 대상 비동기 컴파일 시작
    const bool bStarted = compiler.compileModule( "EditorModule" );
    SW_EXPECT_TRUE( bStarted );
    SW_EXPECT_TRUE( compiler.isCompiling() );

    // 3) 백그라운드 컴파일 완료 대기 (최대 60초)
    const auto startTime = std::chrono::steady_clock::now();
    while ( compiler.isCompiling() )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        manager.update();

        const auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>( std::chrono::steady_clock::now() - startTime ).count();
        if ( elapsedSec > 60 )
            break;
    }

    // 4) 컴파일 성공 후 핫스왑 완료 대기
    for ( int32 stepIndex = 0; stepIndex < 150 && onAfterCalled == false; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
        manager.update();
    }

    SW_EXPECT_FALSE( compiler.isCompiling() );

    // 이 테스트는 "모듈을 다시 빌드해 핫스왑" 을 검증한다. 그런데 EditorModule 을 빌드하면 의존하는
    // Engine 까지 다시 링크되는데, 이 테스트 프로세스가 그 Engine.dll 을 로드하고 있어서 교체할 수
    // 없다. 즉 Engine 이 stale 인 상태에서는 어떤 방법으로도 통과할 수 없는 환경 제약이지
    // 코드 결함이 아니다 — 실패가 아니라 스킵으로 다룬다.
    // (ctest 를 완전한 빌드 뒤에 돌리면 발생하지 않는다. 빌드 후 clang-format 이 소스를 다시 쓰면
    //  Engine 이 stale 이 되어 재현된다.)
    if ( compiler.wasBlockedByLoadedBinary() )
        SW_TEST_SKIP( "Engine.dll is stale and loaded by this process — rebuild before running ctest" );

    SW_EXPECT_EQUAL( static_cast<int32>( sw::BuildState::Success ), static_cast<int32>( compiler.getBuildState() ) );
    SW_EXPECT_EQUAL( 0, compiler.getLastExitCode() );

    // 5) 컴파일 완료 후 LiveReloadManager가 모듈 핫스왑 콜백을 정상 실행했는지 검증
    SW_EXPECT_TRUE( onBeforeCalled );
    SW_EXPECT_TRUE( onAfterCalled );
    SW_ASSERT_NOT_NULL( newHandle );

    // 6) 새로 핫스왑된 모듈에서 C-ABI exportEditorApi 심볼 및 함수 테이블 유효성 검증
    const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
        sw::FileUtil::getDynamicSymbol( newHandle, "exportEditorApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );

    sw::EditorAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );
    SW_EXPECT_TRUE( api.create != nullptr );
    SW_EXPECT_TRUE( api.render != nullptr );

    compiler.shutdown();
    manager.shutdown();
    #endif
}

/**
 * @brief [ArchitectureTest] GPU 없이 MaterialCache acquire/release
 */
SW_TEST_CASE( ArchitectureTest, MaterialCacheAcquireReleaseNoGpu )
{
    sw::MaterialCache& cache = sw::engine::getResourceManager().getMaterialManager();
    cache.clear();

    sw::Material* mat = cache.acquire( "engine/test/does_not_need_gpu.material", nullptr );
    SW_EXPECT_TRUE( mat != nullptr );
    if ( mat == nullptr )
        return;

    sw::Material* again = cache.acquire( "engine/test/does_not_need_gpu.material", nullptr );
    SW_EXPECT_EQUAL( mat, again );

    cache.release( "engine/test/does_not_need_gpu.material" );
    cache.release( "engine/test/does_not_need_gpu.material" );

    // 두 번 잡고 두 번 놓았으니 항목이 사라져 있어야 한다 — **참조 계수 규율은 이것이 전부다.**
    SW_EXPECT_FALSE( cache.isCached( "engine/test/does_not_need_gpu.material" ) );

    // 한 번 더 놓아도 아무 일이 없어야 한다(항목이 이미 없으므로 조용히 돌아간다).
    {
        test::ScopedLogSuppressor suppressor;
        cache.release( "engine/test/does_not_need_gpu.material" );
    }

    // 다시 잡으면 참조가 1 이므로 한 번 놓는 것으로 사라진다 — 앞의 과다 release 가 셈을 흐리지 않았다.
    SW_EXPECT_NOT_NULL( cache.acquire( "engine/test/does_not_need_gpu.material", nullptr ) );
    SW_EXPECT_TRUE( cache.isCached( "engine/test/does_not_need_gpu.material" ) );
    cache.release( "engine/test/does_not_need_gpu.material" );
    SW_EXPECT_FALSE( cache.isCached( "engine/test/does_not_need_gpu.material" ) );

    cache.clear();
}

/**
 * @brief [ArchitectureTest] 4대 RHI 그래픽스 백엔드 (DX11, DX12, Vulkan, GL) 런타임 동적 스왑 및 연속 리로드 검증
 */
SW_TEST_CASE( ArchitectureTest, RHIBackendDynamicSwapAndReload )
{
    const utf8* const kRhiBackends[] = { "RHI_DX11", "RHI_DX12", "RHI_Vulkan", "RHI_GL" };

    for ( int32 cycle = 0; cycle < 2; ++cycle )
    {
        for ( const utf8* backendName : kRhiBackends )
        {
            const sw::string modPath = sw::modulePath( backendName );
            if ( sw::FileUtil::fileExists( modPath ) == false )
                continue;

            void* handle = sw::FileUtil::loadDynamicLibrary( modPath );
            SW_ASSERT_NOT_NULL( handle );

            auto* getStamp = reinterpret_cast<const utf8* (*)()>(
                sw::FileUtil::getDynamicSymbol( handle, "getRHIModuleAbiStamp" ) );
            SW_ASSERT_NOT_NULL( getStamp );
            SW_EXPECT_EQUAL( sw::string( sw::kRHIModuleAbiStamp ), sw::string( getStamp() ) );

            auto* getAbiVer = reinterpret_cast<uint32 ( * )()>(
                sw::FileUtil::getDynamicSymbol( handle, "getRHIModuleAbiVersion" ) );
            SW_ASSERT_NOT_NULL( getAbiVer );
            SW_EXPECT_EQUAL( sw::kRHIModuleAbiVersion, getAbiVer() );

            auto* factory = reinterpret_cast<void* (*)()>(
                sw::FileUtil::getDynamicSymbol( handle, "createRHIDevice" ) );
            SW_ASSERT_NOT_NULL( factory );

            // 동적 언로드 및 해제
            sw::FileUtil::unloadDynamicLibrary( handle );
        }
    }
}

#endif // !SW_SHIPPING

#if defined( SW_SHIPPING )

// ------------------------------------------------------------------------------
// 2) ModuleAPI — exportGameApi / exportEditorApi
// ------------------------------------------------------------------------------
/**
 * @brief [ModuleApiTest] Shipping 정적 exportGameApi
 */
SW_TEST_CASE( ModuleApiTest, ExportGameAPI_ShippingStatic )
{
    sw::GameAPI api{};
    SW_EXPECT_TRUE( exportGameApi( &api ) );
    SW_EXPECT_TRUE( api.create != nullptr );
    SW_EXPECT_TRUE( api.destroy != nullptr );
    SW_EXPECT_TRUE( api.initialize != nullptr );
    SW_EXPECT_TRUE( api.shutdown != nullptr );
    SW_EXPECT_TRUE( api.update != nullptr );

    sw::GameHandle game = api.create();
    SW_EXPECT_TRUE( game != nullptr );
    if ( game != nullptr )
        api.destroy( game );
}

#else

/**
 * @brief [ModuleApiTest] SWGame DLL exportGameApi
 */
SW_TEST_CASE( ModuleApiTest, ExportGameAPI )
{
    void* handle = sw::loadModule( "SWGame" );
    SW_EXPECT_TRUE( handle != nullptr );
    if ( handle == nullptr )
        return;

    const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>(
        sw::FileUtil::getDynamicSymbol( handle, "exportGameApi" ) );
    SW_EXPECT_TRUE( pfnExport != nullptr );
    if ( pfnExport == nullptr )
    {
        sw::FileUtil::unloadDynamicLibrary( handle );
        return;
    }

    sw::GameAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );
    SW_EXPECT_TRUE( api.create != nullptr );
    SW_EXPECT_TRUE( api.destroy != nullptr );
    SW_EXPECT_TRUE( api.initialize != nullptr );
    SW_EXPECT_TRUE( api.shutdown != nullptr );
    SW_EXPECT_TRUE( api.update != nullptr );
    // create()/destroy()/lifecycle 은 FullGameSceneAndComponentLifecycle 에서 검증

    sw::engine::registerModuleTypes( "SWGame" );
    sw::engine::unregisterModuleTypes( "SWGame" );

    sw::FileUtil::unloadDynamicLibrary( handle );
}

/**
 * @brief [ModuleApiTest] Full Game Scene, Component Lifecycle & Tick Verification
 */
SW_TEST_CASE( ModuleApiTest, FullGameSceneAndComponentLifecycle )
{
    void* hOverworld = sw::loadModule( "GF_Overworld" );
    if ( hOverworld != nullptr )
        sw::engine::registerModuleTypes( "GF_Overworld" );

    void* handle = sw::loadModule( "SWGame" );
    SW_EXPECT_TRUE( handle != nullptr );
    if ( handle == nullptr )
    {
        if ( hOverworld != nullptr )
        {
            sw::engine::unregisterModuleTypes( "GF_Overworld" );
            sw::FileUtil::unloadDynamicLibrary( hOverworld );
        }
        return;
    }

    sw::engine::registerModuleTypes( "SWGame" );

    const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::FileUtil::getDynamicSymbol( handle, "exportGameApi" ) );
    SW_EXPECT_TRUE( pfnExport != nullptr );
    if ( pfnExport == nullptr )
    {
        sw::engine::unregisterModuleTypes( "SWGame" );
        if ( hOverworld != nullptr )
        {
            sw::engine::unregisterModuleTypes( "GF_Overworld" );
            sw::FileUtil::unloadDynamicLibrary( hOverworld );
        }
        sw::FileUtil::unloadDynamicLibrary( handle );
        return;
    }

    sw::GameAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );

    if ( api.bindService )
    {
        sw::ModuleService gameService{};
        sw::engine::fillModuleServices( gameService, true );
        api.bindService( &gameService );
    }

    sw::GameHandle game = api.create();
    SW_ASSERT_NOT_NULL( game );

    SW_EXPECT_TRUE( api.initialize( game, nullptr, nullptr ) );

    // 1) Test initial game updates
    for ( int32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        api.update( game, 0.016f );
    }

    // 2) Create and tick a dynamic test scene
    sw::Scene* pTestScene = sw::engine::getSceneManager().createScene( "TestMainScene" );
    SW_ASSERT_NOT_NULL( pTestScene );
    sw::GameObject* pObj = pTestScene->getObjectManager()->createGameObject( sw::hashed_string( "TestActor" ) );
    SW_ASSERT_NOT_NULL( pObj );
    SW_EXPECT_TRUE( pTestScene->getObjectManager()->getAllGameObjects().size() > 0 );

    for ( int32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        api.update( game, 0.016f );
        sw::Scene* pActive = sw::engine::getSceneManager().getActiveScene();
        if ( pActive != nullptr )
            pActive->tick( 0.016f );
    }

    SW_LOG_INFO( "[SmokeTest] Step 11: Shutting down..." );

    sw::engine::getTaskManager().waitAll();
    sw::engine::getSceneManager().tickTransitions();

    api.shutdown( game );
    api.destroy( game );

    if ( api.bindService )
        api.bindService( nullptr );

    sw::engine::getSceneManager().shutdown();
    sw::engine::getSceneManager().initialize();

    sw::engine::unregisterModuleTypes( "SWGame" );
    sw::FileUtil::unloadDynamicLibrary( handle );

    if ( hOverworld != nullptr )
    {
        sw::engine::unregisterModuleTypes( "GF_Overworld" );
        sw::FileUtil::unloadDynamicLibrary( hOverworld );
    }
}

/**
 * @brief [ModuleApiTest] EditorModule DLL exportEditorApi
 */
SW_TEST_CASE( ModuleApiTest, ExportEditorAPI )
{
    void* handle = sw::loadModule( "EditorModule" );
    SW_ASSERT_NOT_NULL( handle );

    const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
        sw::FileUtil::getDynamicSymbol( handle, "exportEditorApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );

    sw::EditorAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );
    SW_ASSERT_NOT_NULL( api.create );

    sw::engine::registerModuleTypes( "EditorModule" );
    sw::engine::unregisterModuleTypes( "EditorModule" );

    sw::FileUtil::unloadDynamicLibrary( handle );
}

/**
 * @brief [ModuleApiTest] 장르별 독립 Kit 모듈 (GF_Overworld, GF_TurnBattle, GF_ActionCombat) 타입 등록 검증
 */
SW_TEST_CASE( ModuleApiTest, GameFrameworkKitsModuleTypeRegistration )
{
    for ( const utf8* kitName : { "GF_Overworld", "GF_TurnBattle", "GF_ActionCombat" } )
    {
        void* handle = sw::loadModule( kitName );
        if ( handle )
        {
            sw::engine::registerModuleTypes( kitName );
            sw::engine::unregisterModuleTypes( kitName );
            sw::FileUtil::unloadDynamicLibrary( handle );
        }
    }
}

/**
 * @brief [ModuleApiTest] SWGame 모듈 반복 로드/언로드 사이클 안정성
 */
SW_TEST_CASE( ModuleApiTest, GameModuleRepeatedReloadCycle )
{
    for ( int32 cycle = 0; cycle < 2; ++cycle )
    {
        void* hOverworld = sw::loadModule( "GF_Overworld" );
        if ( hOverworld != nullptr )
            sw::engine::registerModuleTypes( "GF_Overworld" );

        void* handle = sw::loadModule( "SWGame" );
        SW_ASSERT_NOT_NULL( handle );

        sw::engine::registerModuleTypes( "SWGame" );

        const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>(
            sw::FileUtil::getDynamicSymbol( handle, "exportGameApi" ) );
        SW_ASSERT_NOT_NULL( pfnExport );

        sw::GameAPI api{};
        SW_EXPECT_TRUE( pfnExport( &api ) );

        if ( api.bindService )
        {
            sw::ModuleService gameService{};
            sw::engine::fillModuleServices( gameService, true );
            api.bindService( &gameService );
        }

        sw::GameHandle game = api.create();
        SW_ASSERT_NOT_NULL( game );
        SW_EXPECT_TRUE( api.initialize( game, nullptr, nullptr ) );

        // 가벼운 틱 실행 (비동기 씬 로드 완료 대기)
        for ( int32 frame = 0; frame < 20; ++frame )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            sw::engine::getSceneManager().tickTransitions();
            api.update( game, 0.016f );
        }
        sw::engine::getTaskManager().waitAll();
        sw::engine::getSceneManager().tickTransitions();

        api.shutdown( game );
        api.destroy( game );

        if ( api.bindService )
            api.bindService( nullptr );

        sw::engine::getSceneManager().shutdown();
        sw::engine::getSceneManager().initialize();

        sw::engine::unregisterModuleTypes( "SWGame" );
        sw::FileUtil::unloadDynamicLibrary( handle );

        if ( hOverworld != nullptr )
        {
            sw::engine::unregisterModuleTypes( "GF_Overworld" );
            sw::FileUtil::unloadDynamicLibrary( hOverworld );
        }
    }
}

#endif // SW_SHIPPING
