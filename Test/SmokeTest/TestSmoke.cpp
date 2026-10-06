#include "pch.h"

#include "App/Module/LiveReloadManager.h"
#include "App/Module/ModuleCompiler.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/Process/Process.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/Modules/RHIModuleAbi.h"
#include "Engine/Module/EngineAbiStamp.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/ObjectUndoUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/WindowEvents.h"

#include "GameFramework/Base/Framework/GameService.h"
#include "GameFramework/GameFrameworkExports.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/ABI/GameAPI.h"

#include "TestFramework/TestChildProcess.h"
#include "TestFramework/TestFramework.h"
#include "TestFramework/TestPropertyCoverage.h"

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
            return dir + "/" + sw::ModuleImageUtil::formatSharedLibraryName( pBaseName );
        }

        /**
         * @brief 모듈 폴더에 남은 @p pModuleName 의 섀도 복사본 파일(DLL · 디버그 심볼) 가운데 **이 프로세스가 만든 것**의 수를 셉니다.
         * @details 그 폴더는 나란히 도는 다른 시험 프로세스(App 을 띄우는 AppTest 등)도 쓰므로 그쪽 복사본은 세지 않습니다.
         */
        uint32 countShadowCopies( const utf8* pModuleName )
        {
            const sw::string       directory = sw::FileUtil::getDirectoryPart( modulePath( pModuleName ) );
            sw::vector<sw::string> listFile;
            if ( sw::FileUtil::collectFiles( directory, "", listFile, false ) == false )
                return 0;
            const sw::string prefix           = sw::string{ pModuleName } + sw::ShadowCopyName::kMarker;
            const int32      currentProcessId = sw::Process::getCurrentProcessId();
            uint32           count{ 0 };
            for ( const sw::string& filePath : listFile )
            {
                int32      ownerProcessId{ 0 };
                const bool bOwnCopy = filePath.find( prefix ) != sw::string::npos && sw::ShadowCopyName::parse( filePath, ownerProcessId ) &&
                                      ownerProcessId == currentProcessId;
                if ( bOwnCopy )
                    ++count;
            }
            return count;
        }

        /** @brief 테스트 모듈을 동적 로드합니다. */
        void* loadModule( const utf8* pName )
        {
            const sw::string path = modulePath( pName );
            return sw::ModuleImageUtil::loadDynamicLibrary( path );
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
            const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::ModuleImageUtil::getDynamicSymbol( pGameModule, "exportGameApi" ) );
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

        /** @brief 델리게이트의 스텁 하나만 담는 범위로 `ModuleImageUtil::releaseModuleCode` 를 부릅니다. */
        template <typename TDelegate>
        uint32 releaseStubOf( const TDelegate& delegate )
        {
            const uint8* pCode = static_cast<const uint8*>( delegate.getCodeAddress() );
            return sw::ModuleImageUtil::releaseModuleCode( "SweepProbe", pCode, pCode + 1 );
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
        if ( sw::FileUtil::exists( path ) == false )
            continue;

        void* handle = sw::ModuleImageUtil::loadDynamicLibrary( path );
        SW_EXPECT_TRUE( handle != nullptr );
        if ( handle == nullptr )
            continue;

        const sw::PFN_GetRHIModuleAbiVersion pfnVersion = reinterpret_cast<sw::PFN_GetRHIModuleAbiVersion>(
            sw::ModuleImageUtil::getDynamicSymbol( handle, "getRHIModuleAbiVersion" ) );
        const sw::PFN_GetRHIModuleAbiStamp pfnStamp = reinterpret_cast<sw::PFN_GetRHIModuleAbiStamp>(
            sw::ModuleImageUtil::getDynamicSymbol( handle, "getRHIModuleAbiStamp" ) );
        const void* pfnCreate = reinterpret_cast<void*>(
            sw::ModuleImageUtil::getDynamicSymbol( handle, "createRHIDevice" ) );

        SW_EXPECT_TRUE( pfnVersion != nullptr );
        SW_EXPECT_TRUE( pfnStamp != nullptr );
        SW_EXPECT_TRUE( pfnCreate != nullptr );

        if ( pfnVersion != nullptr )
            SW_EXPECT_EQUAL( sw::kRHIModuleAbiVersion, pfnVersion() );
        if ( pfnStamp != nullptr && pfnStamp() != nullptr )
            SW_EXPECT_STREQ( sw::kRHIModuleAbiStamp, pfnStamp() );

        SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( modName, handle ) );
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
 * @brief 깨진 상태로 표시된 LiveReload 그래프는 이후 triggerReload 를 무시한다
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadBrokenGraphIgnoresTrigger )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing broken reload graph handling" );
    sw::LiveReloadManager manager;
    SW_EXPECT_FALSE( manager.isGraphBroken() );
    manager.markGraphBroken( "test" );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
    manager.triggerReload( "SWGame" );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
}

/**
 * @brief onAfter 가 그래프를 깨진 상태로 표시하면 registerModule 은 실패해야 하고, 올렸던 이미지와 섀도 복사본은 그 자리에서 치운다
 * @details commit 은 새 핸들을 컨텍스트에 넣은 뒤 onAfter 가 그래프를 막아도 실패를 돌려준다. 등록이 그 컨텍스트를 내리지 않고 지우면
 *          이미지 · 섀도 파일(`SWGame_temp_*`)이 프로세스 끝까지 남는다.
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadOnAfterBrokenGraphFailsRegister )
{
    if ( sw::FileUtil::exists( sw::modulePath( "SWGame" ) ) == false )
        SW_TEST_SKIP( "SWGame MODULE not built in this config" );
    SW_TEST_DEFENSIVE_SCOPE( "Testing registration failure when onAfter breaks the graph" );
    const uint32 shadowCountBefore = sw::countShadowCopies( "SWGame" );

    sw::LiveReloadManager manager;
    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&manager]( void* )
    {
        manager.markGraphBroken( "test onAfter" );
    } ) );
    SW_EXPECT_FALSE( manager.registerModule( "SWGame" ) );
    SW_EXPECT_TRUE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( manager.getModuleHandle( "SWGame" ) == nullptr );
    SW_EXPECT_EQUAL( shadowCountBefore, sw::countShadowCopies( "SWGame" ) );
    manager.shutdown();
}

/**
 * @brief 캐스케이드 중 앞 모듈의 onAfter 가 그래프를 깨진 상태로 표시하면 이후 모듈은 commit 하지 않는다
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadCascadeAbortsAfterOnAfterBrokenGraph )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing cascade abort when a dependency's onAfter breaks the graph" );
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::exists( gfPath ) == false )
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
    if ( sw::FileUtil::exists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::TypeRegistry& registry = sw::engine::getTypeRegistry();

    const auto collectFqn = [&registry]()
    {
        sw::vector<sw::hashed_string> listFqn;
        registry.forEachType( [&listFqn]( const sw::TypeInfo& info )
        {
            listFqn.push_back( info._fullyQualifiedName );
        } );
        std::sort( listFqn.begin(), listFqn.end(), sw::HashedStringFastLess{} ); // 찾기용 정렬이다 — 순서에 뜻은 없다
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
        if ( std::binary_search( baseTypes.begin(), baseTypes.end(), fqn, sw::HashedStringFastLess{} ) == false )
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
    if ( sw::FileUtil::exists( gfPath ) == false )
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
 * @details `ModuleImageUtil::releaseModuleCode` 는 모듈이 스스로 떼지 않은 것을 떼는 안전망이다. 에디터는 이벤트 구독 · 로그 리스너(콘솔 패널) ·
 *          Undo 명령(트랜잭션) · 창 닫기 처리기를 달고, `ImGuiEditor::shutdown` · `~ConsolePanel` 이 손으로 뗀다. 여기서는 등록부마다
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
 *          접근 위반이 나면 에디터째 내려가지 않고, 그래프를 막고 결함 콜백을 부른다 — ModuleHost 는 그 콜백에서 받은 것을
 *          모듈을 부르지 않고 버린다. 여기서는 콜백 자리에 일부러 널 쓰기를 둔다.
 */
SW_TEST_CASE( ArchitectureTest, FaultInOnAfterReloadStopsTheModuleNotTheProcess )
{
    const sw::string gamePath = sw::modulePath( "SWGame" );
    if ( sw::FileUtil::exists( gamePath ) == false )
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
 * @brief [ArchitectureTest] 모듈의 전역 변수는 모듈을 올리는 쪽이 모듈 이름으로 등록하고, 내리는 쪽이 걷는다
 * @details 모듈 코드는 등록 · 해제를 부르지 않는다. 등록자는 타입 등록자처럼 정적 초기화 때 전역 헤드에 매달리고, `LiveReloadManager` 가
 *          로드 직후 떼어 모듈 이름으로 올린다(커맨드라인 보류값도 이때 적용된다). 헤드는 다시 비워져야 한다 — 남으면 다음에 떼는 쪽이
 *          내려간 이미지의 등록자를 따라간다. 리로드 뒤에도 그대로 있고, 내리면 사라진다.
 */
SW_TEST_CASE( ArchitectureTest, ModuleGlobalVariablesFollowTheModuleLifetime )
{
    if ( sw::FileUtil::exists( sw::modulePath( "EditorModule" ) ) == false )
        SW_TEST_SKIP( "EditorModule not built in this config" );
    if ( sw::engine::areEngineServicesBound() == false )
        SW_TEST_SKIP( "engine services not bound" );

    sw::GlobalVariableManager& variableManager = sw::engine::getGlobalVariableManager();
    SW_ASSERT_TRUE( variableManager.findVariable( "gv_editorPanelDump" ) == nullptr );

    sw::LiveReloadManager manager;
    SW_ASSERT_TRUE( manager.registerModule( "EditorModule" ) );
    const sw::GlobalVariableInfo* pRegistered = variableManager.findVariable( "gv_editorPanelDump" );
    SW_ASSERT_TRUE( pRegistered != nullptr );
    SW_EXPECT_STREQ( "EditorModule", pRegistered->_moduleName.c_str() );
    SW_EXPECT_TRUE( sw::GlobalVariableRegistrar::getHead() == nullptr );

    SW_ASSERT_TRUE( sw::reloadAndWait( manager, "EditorModule" ) );
    SW_EXPECT_TRUE( variableManager.findVariable( "gv_editorPanelDump" ) != nullptr );
    SW_EXPECT_TRUE( sw::GlobalVariableRegistrar::getHead() == nullptr );

    manager.shutdown();
    SW_EXPECT_TRUE( variableManager.findVariable( "gv_editorPanelDump" ) == nullptr );
}

/**
 * @brief [ArchitectureTest] EditorModule DLL 독립 LiveReload 및 C-ABI 테이블 재바인딩 검증
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadEditorModule )
{
    const sw::string editorPath = sw::modulePath( "EditorModule" );
    if ( sw::FileUtil::exists( editorPath ) == false )
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
        sw::ModuleImageUtil::getDynamicSymbol( newHandle, "exportEditorApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );

    sw::EditorAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );
    SW_EXPECT_TRUE( api.create != nullptr );
    SW_EXPECT_TRUE( api.render != nullptr );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 에디터 모듈을 실제로 다시 올려도 오브젝트 편집의 Undo/Redo 가 남아 같은 결과를 낸다
 * @details 리로드는 옛 이미지를 내리기 전에 그 범위로 모든 언로드 리스너를 훑는다(`ModuleImageUtil::releaseModuleCode` — Undo 스택 포함). 오브젝트 편집은 코드가
 *          Engine 에 있는 데이터 명령(`ObjectUndoUtil`)이라 그 훑기를 지나 남아야 하고, 리로드 뒤의 undo · redo 가 리로드 전과 같은 상태를 만들어야
 *          한다. 모듈 코드를 쥔 명령이 떨어지는 쪽은 `EditorTransactionTest.ObjectEditsSurviveReleasingTheEditorCode` ·
 *          `ReleaseModuleCodeSweepsEveryRegistryTheEditorUses` 가 본다.
 */
SW_TEST_CASE( ArchitectureTest, ObjectUndoSurvivesAnEditorModuleReload )
{
    if ( sw::FileUtil::exists( sw::modulePath( "EditorModule" ) ) == false )
        SW_TEST_SKIP( "EditorModule not built in this config" );

    sw::SceneManager sceneManager;
    sw::Scene*       pScene = sceneManager.createEmptyActiveScene( "UndoAcrossReload" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pManager = pScene->getObjectManager();
    sw::GameObject*        pTarget  = pManager->createGameObject( sw::hashed_string( "UndoTarget" ) );
    SW_ASSERT_NOT_NULL( pTarget );
    pManager->mergePendingAdds();
    const uint64 targetId = pTarget->getObjectId();

    sw::CommandStack         stack;
    const sw::ObjectSnapshot before = sw::ObjectUndoUtil::captureSnapshot( pTarget );
    pTarget->setActive( false );
    const sw::ObjectSnapshot after = sw::ObjectUndoUtil::captureSnapshot( pTarget );
    SW_ASSERT_TRUE( before._xml != after._xml );
    stack.push( sw::ObjectUndoUtil::makeModify( stack, sceneManager, *pTarget, before, after, "Deactivate" ) );

    sw::LiveReloadManager manager;
    SW_ASSERT_TRUE( manager.registerModule( "EditorModule" ) );
    void* const pOldHandle = manager.getModuleHandle( "EditorModule" );
    SW_ASSERT_TRUE( sw::reloadAndWait( manager, "EditorModule" ) );
    SW_EXPECT_TRUE( manager.getModuleHandle( "EditorModule" ) != pOldHandle );
    SW_EXPECT_FALSE( manager.isGraphBroken() );

    SW_ASSERT_EQUAL( size_t( 1 ), stack.getCommandCount() );
    stack.undo();
    SW_EXPECT_TRUE( sw::ObjectUndoUtil::captureSnapshot( pManager->findGameObjectById( targetId ) )._xml == before._xml );
    stack.redo();
    SW_EXPECT_TRUE( sw::ObjectUndoUtil::captureSnapshot( pManager->findGameObjectById( targetId ) )._xml == after._xml );
    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 장르 키트 모듈 (GF_Overworld, GF_MonsterCollector, GF_ActionCombat) 개별 LiveReload 검증
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadGenreKitsIndividuallyAndCascaded )
{
    const utf8* kKits[] = { "GF_Overworld", "GF_MonsterCollector", "GF_ActionCombat" };

    for ( const utf8* kitName : kKits )
    {
        const sw::string kitPath = sw::modulePath( kitName );
        if ( sw::FileUtil::exists( kitPath ) == false )
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
 * @brief [ArchitectureTest] 키트 둘(GF_Farming · GF_CreatureLife)에 기대는 게임 하나 — 한 키트를 리로드하면 게임은 다시 서고 다른 키트는 그대로다
 */
SW_TEST_CASE( ArchitectureTest, LiveReloadOneOfTwoKitsCascadesIntoTheGameOnly )
{
    for ( const utf8* pKitName : { "GF_Farming", "GF_CreatureLife" } )
    {
        if ( sw::FileUtil::exists( sw::modulePath( pKitName ) ) == false )
            SW_TEST_SKIP( "kit module is not built" );
    }
    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GF_Farming" ) == false || manager.registerModule( "GF_CreatureLife" ) == false )
        SW_TEST_SKIP( "kit registration failed" );
    sw::vector<sw::string> gameDepends;
    gameDepends.push_back( "GF_Farming" );
    gameDepends.push_back( "GF_CreatureLife" );
    if ( manager.registerModule( "SWGame", gameDepends ) == false )
        SW_TEST_SKIP( "SWGame registration failed" );

    bool farmingReloaded{ false };
    bool creatureReloaded{ false };
    bool gameReloaded{ false };
    manager.setOnAfterReload(
        "GF_Farming",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&farmingReloaded]( void* )
    {
        farmingReloaded = true;
    } ) );
    manager.setOnAfterReload(
        "GF_CreatureLife",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&creatureReloaded]( void* )
    {
        creatureReloaded = true;
    } ) );
    manager.setOnAfterReload(
        "SWGame",
        SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&gameReloaded]( void* )
    {
        gameReloaded = true;
    } ) );

    manager.triggerReload( "GF_Farming" );
    for ( int32 stepIndex = 0; stepIndex < 100; ++stepIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
        manager.update();
        if ( farmingReloaded && gameReloaded )
            break;
    }

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    SW_EXPECT_TRUE( farmingReloaded );
    SW_EXPECT_TRUE( gameReloaded );
    SW_EXPECT_FALSE( creatureReloaded ); // 다른 키트는 그대로
    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] App 과 같은 사슬(GameFramework → 킷 → SWGame)을 연쇄 리로드해도, 의존 모듈은 **지금의** 복사본에 묶인다
 * @details 섀도 복사본은 파일 이름이 원본과 달라서 의존 모듈이 어느 이미지에 묶일지를 로더가 정한다. Windows 는 지연 로드 훅이
 *          `LiveReloadManager` 에게 물어 지금의 복사본을 받고, 리눅스는 SONAME 이 같은 **먼저 올라온** 이미지가 이긴다. 어긋나면
 *          GameFramework 가 한 프로세스에 두 벌 돌고, 옛 복사본을 내리는 순간 그리로 뛰는 코드가 죽는다. 리로드가 "끝났다" 만 보면
 *          이것을 못 잡으므로 누가 누구에게 묶였는지를 본다.
 *
 *          Windows 는 올리는 자리(`LiveReloadManager` 의 커밋)가 지연 import 를 미리 묶으므로(`ModuleImageUtil::bindDelayLoadImports`) 모듈 코드를
 *          부르기 전에 이미 지금의 GameFramework 에 묶여 있어야 한다. 리눅스는 `RTLD_NOW` 라 로드하는 순간 모두 묶인다.
 */
SW_TEST_CASE( ArchitectureTest, ReloadedDependentsBindToTheCurrentImages )
{
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::exists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GameFramework" ) == false )
        SW_TEST_SKIP( "GameFramework registration failed" );
    if ( manager.registerModule( "GF_Overworld", { "GameFramework" } ) == false )
        SW_TEST_SKIP( "GF_Overworld registration failed" );
    if ( manager.registerModule( "SWGame", { "GameFramework", "GF_Overworld" } ) == false )
        SW_TEST_SKIP( "SWGame registration failed" );

    SW_EXPECT_FALSE( manager.isGraphBroken() );
    #if defined( SW_PLATFORM_WINDOWS )
    // 올리는 자리가 지연 import 를 미리 묶는다(첫 호출이 묶으면 첫 float 인자가 망가진다) — 모듈 코드를 부르기 전에 이미 지금의 GameFramework 에 묶여 있다.
    SW_EXPECT_TRUE( sw::ModuleImageUtil::findBoundImportImage( manager.getModuleHandle( "SWGame" ), "GameFramework.dll" ) == manager.getModuleHandle( "GameFramework" ) );
    SW_EXPECT_TRUE( sw::ModuleImageUtil::findBoundImportImage( manager.getModuleHandle( "GF_Overworld" ), "GameFramework.dll" ) == manager.getModuleHandle( "GameFramework" ) );
    #endif
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
    #if defined( SW_PLATFORM_WINDOWS )
    // 올리는 자리가 지연 import 를 미리 묶는다(첫 호출이 묶으면 첫 float 인자가 망가진다) — 모듈 코드를 부르기 전에 이미 지금의 GameFramework 에 묶여 있다.
    SW_EXPECT_TRUE( sw::ModuleImageUtil::findBoundImportImage( manager.getModuleHandle( "SWGame" ), "GameFramework.dll" ) == manager.getModuleHandle( "GameFramework" ) );
    SW_EXPECT_TRUE( sw::ModuleImageUtil::findBoundImportImage( manager.getModuleHandle( "GF_Overworld" ), "GameFramework.dll" ) == manager.getModuleHandle( "GameFramework" ) );
    #endif
    SW_EXPECT_TRUE( sw::createAndDestroyGame( manager.getModuleHandle( "SWGame" ) ) );
    SW_EXPECT_TRUE( manager.verifyModuleBindings() );

    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 이 프로세스가 시킨 빌드가 도는 동안은 모듈을 올리지 않고, 끝나면 성공일 때만 올린다
 * @details 연쇄 빌드는 모듈 DLL 을 하나씩 다시 쓴다. 파일 감시만 믿고 mtime 이 디바운스 시간(300 ms)만큼 멈출 때 올리면, 의존하는 모듈의 링크가 끝나기
 *          전의 반쯤 된 집합이 올라갈 수 있다. 빌드 중에는 예약(강제 리로드 포함)을 모아 두기만 하고, 빌드가 성공하면 올리고 실패하면 버린다.
 */
SW_TEST_CASE( ArchitectureTest, ReloadWaitsForTheBuildToSucceed )
{
    if ( sw::FileUtil::exists( sw::modulePath( "SWGame" ) ) == false )
        SW_TEST_SKIP( "SWGame MODULE not built in this config" );

    sw::LiveReloadManager manager;
    SW_ASSERT_TRUE( manager.registerModule( "SWGame" ) );
    uint32 reloadCount{ 0 };
    manager.setOnAfterReload( "SWGame", SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&reloadCount]( void* )
    { ++reloadCount; } ) );

    // 디바운스(300 ms)를 넉넉히 넘게 돌린다.
    sw::Delegate<void()> pump = SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [&manager]()
    {
        for ( int32 stepIndex = 0; stepIndex < 50; ++stepIndex )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
            manager.update();
        }
    } );

    // 빌드 중: 예약은 올라가지 않는다.
    manager.notifyBuildStarted();
    manager.triggerReload( "SWGame" );
    pump();
    SW_EXPECT_EQUAL( 0u, reloadCount );

    // 성공: 기다리던 예약이 올라간다.
    manager.notifyBuildFinished( true, "" );
    pump();
    SW_EXPECT_EQUAL( 1u, reloadCount );

    // 실패: 그 빌드 동안의 예약은 버린다.
    manager.notifyBuildStarted();
    manager.triggerReload( "SWGame" );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a failed build drops the reload it held" );
        manager.notifyBuildFinished( false, "" );
        pump();
    }
    SW_EXPECT_EQUAL( 1u, reloadCount );

    // 성공한 빌드의 타깃은 바뀌지 않았어도 다시 올린다(에디터의 Compile 버튼).
    manager.notifyBuildStarted();
    manager.notifyBuildFinished( true, "SWGame" );
    pump();
    SW_EXPECT_EQUAL( 2u, reloadCount );

    manager.setOnAfterReload( "SWGame", sw::LiveReloadManager::OnAfterReloadDelegate{} );
    manager.shutdown();
}

/**
 * @brief [ArchitectureTest] 교체된 옛 이미지는 바로 내려가지 않고, 배치가 상한을 넘을 때 오래된 것부터 내려간다
 * @details 옛 코드를 가리키는 것이 남아 있어도 이미지가 올라와 있는 동안은 크래시가 아니라 옛 동작이 한 번 더 돈다. 그래서 첫 이미지에서
 *          얻은 함수 포인터를 리로드 **뒤에** 불러 본다 — 바로 `FreeLibrary` 하면 이 호출이 내려간 코드로 뛴다. 상한보다 많이
 *          리로드하면 언로드를 미룬 이미지 수는 상한에서 멈추고, 종료하면 모두 내려간다.
 */
SW_TEST_CASE( ArchitectureTest, DeferredUnloadImagesStayMappedUntilTheirBatchIsEvicted )
{
    const sw::string gfPath = sw::modulePath( "GameFramework" );
    if ( sw::FileUtil::exists( gfPath ) == false )
        SW_TEST_SKIP( "GameFramework MODULE not built in this config" );

    sw::LiveReloadManager manager;
    if ( manager.registerModule( "GameFramework" ) == false )
        SW_TEST_SKIP( "GameFramework registration failed" );
    if ( manager.registerModule( "SWGame", { "GameFramework" } ) == false )
        SW_TEST_SKIP( "SWGame registration failed" );
    SW_EXPECT_EQUAL( 0u, manager.getDeferredUnloadImageCount() );

    void* const                 pFirstGame     = manager.getModuleHandle( "SWGame" );
    const sw::PFN_ExportGameAPI pfnFirstExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::ModuleImageUtil::getDynamicSymbol( pFirstGame, "exportGameApi" ) );
    SW_ASSERT_TRUE( pfnFirstExport != nullptr );

    SW_ASSERT_TRUE( sw::reloadAndWait( manager, "SWGame" ) );
    SW_EXPECT_TRUE( manager.getModuleHandle( "SWGame" ) != pFirstGame );
    SW_EXPECT_EQUAL( 1u, manager.getDeferredUnloadImageCount() );

    // 첫 이미지는 아직 올라와 있다 — 그 코드를 불러도 된다.
    sw::GameAPI firstApi{};
    SW_EXPECT_TRUE( pfnFirstExport( &firstApi ) );

    for ( uint32 reloadIndex = 0; reloadIndex < sw::LiveReloadManager::kMaxDeferredUnloadBatchCount + 2; ++reloadIndex )
    {
        SW_ASSERT_TRUE( sw::reloadAndWait( manager, "SWGame" ) );
    }
    // SWGame 만 바뀌는 연쇄는 배치 하나에 이미지 하나다. 상한에서 멈춘다.
    SW_EXPECT_EQUAL( sw::LiveReloadManager::kMaxDeferredUnloadBatchCount, manager.getDeferredUnloadImageCount() );
    SW_EXPECT_FALSE( manager.isGraphBroken() );

    manager.shutdown();
    SW_EXPECT_EQUAL( 0u, manager.getDeferredUnloadImageCount() );
}

/**
 * @brief [ArchitectureTest] 빌드된 모듈은 돌고 있는 엔진과 같은 ABI 도장을 들고 있다
 * @details 도장은 Core · Engine 헤더 내용의 지문이고 Engine 과 모듈이 같은 생성 헤더로 박는다. 이 테스트는 빌드 연결(생성 소스가 모듈마다
 *          들어가고, 링커가 지우지 않는다)을 실제 파일로 확인한다.
 */
SW_TEST_CASE( ArchitectureTest, ModulesCarryTheRunningEngineAbiStamp )
{
    // 에디터도 같은 매니저로 리로드된다 — 도장이 빠지면 에디터만 헤더가 어긋난 채 올라간다.
    const utf8* arrModuleName[] = { "GameFramework", "SWGame", "EditorModule" };
    for ( const utf8* pModuleName : arrModuleName )
    {
        const sw::string path = sw::modulePath( pModuleName );
        if ( sw::FileUtil::exists( path ) == false )
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
    if ( sw::FileUtil::exists( gamePath ) == false )
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

    SW_EXPECT_TRUE( sw::FileUtil::removeFile( probePath ) );
}

namespace sw
{
    namespace
    {
        /** @brief 적용 전 실패를 주입하는 자리입니다. */
        enum class PreApplyFailure : uint8
        {
            ImageRejected, ///< 이미지 검사(`setOnValidateImage`)가 새 이미지를 거절한다 — 모듈 API 표가 다른 빌드
            BatchRefused,  ///< 배치 직전 콜백(`setOnBeforeCommitBatch`)이 거절한다 — 게임 상태를 찍지 못했다
        };

        /**
         * @brief SWGame 리로드에 @p failure 를 주입하고, 옛 모듈이 그대로 남아 돌며 그래프가 막히지 않았는지 봅니다. 주입을 거두면 리로드가 됩니다.
         * @return 주입 자리가 불렸으면 true 입니다(불리지 않으면 케이스가 아무것도 검증하지 못한다).
         */
        bool expectPreApplyFailureKeepsTheOldModule( PreApplyFailure failure )
        {
            sw::LiveReloadManager manager;
            if ( manager.registerModule( "SWGame" ) == false )
                return false;
            void* const pOldHandle = manager.getModuleHandle( "SWGame" );

            bool bInjected{ false };
            bool bBeforeReloadCalled{ false };
            manager.setOnBeforeReload( "SWGame", SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeReloadDelegate, [&bBeforeReloadCalled]()
            {
                bBeforeReloadCalled = true;
            } ) );
            if ( failure == PreApplyFailure::ImageRejected )
            {
                manager.setOnValidateImage( "SWGame", SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnValidateImageDelegate, [&bInjected, pOldHandle]( void* pNewHandle )
                {
                    // 첫 로드(등록)는 받아들이고, 다시 올리는 이미지만 거절한다.
                    if ( pNewHandle == pOldHandle )
                        return true;
                    bInjected = true;
                    return false;
                } ) );
            }
            else
            {
                manager.setOnBeforeCommitBatch( SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnBeforeCommitBatchDelegate, [&bInjected]( const sw::vector<sw::string>& )
                {
                    bInjected = true;
                    return false;
                } ) );
            }

            manager.triggerReload( "SWGame" );
            for ( int32 stepIndex = 0; stepIndex < 100 && bInjected == false; ++stepIndex )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
                manager.update();
            }

            SW_EXPECT_TRUE( bInjected );
            SW_EXPECT_FALSE_MSG( manager.isGraphBroken(), "a failure before the new image took over must not block later reloads" );
            SW_EXPECT_TRUE_MSG( manager.getModuleHandle( "SWGame" ) == pOldHandle, "the old module must stay loaded" );
            SW_EXPECT_FALSE_MSG( bBeforeReloadCalled, "the old module was told to tear down for a reload that did not happen" );
            // 옛 모듈의 함수가 그대로 불린다.
            SW_EXPECT_TRUE( sw::createAndDestroyGame( manager.getModuleHandle( "SWGame" ) ) );
            // 새 이미지의 섀도 복사본은 치웠다 — 남은 것은 지금 올라온 옛 것(DLL · 심볼)뿐이다.
            SW_EXPECT_TRUE( sw::countShadowCopies( "SWGame" ) <= 2u );

            // 주입을 거두면 같은 매니저로 리로드가 된다(그래프가 살아 있다).
            manager.setOnValidateImage( "SWGame", {} );
            manager.setOnBeforeCommitBatch( {} );
            manager.setOnBeforeReload( "SWGame", {} );
            SW_EXPECT_TRUE( sw::reloadAndWait( manager, "SWGame" ) );
            SW_EXPECT_TRUE( manager.getModuleHandle( "SWGame" ) != pOldHandle );
            manager.shutdown();
            return bInjected;
        }
    } // namespace
} // namespace sw

/**
 * @brief [ArchitectureTest] 적용 전 실패(새 이미지 거절 · 배치 거절)는 옛 모듈을 그대로 두고 계속 돈다 — 그래프를 막지 않는다
 * @details 새 이미지가 상태를 넘겨받기 전의 실패는 잃은 것이 없다. 옛 이미지에 onBeforeReload 를 부르지 않고(옛 인스턴스를 내리지 않는다) 새 이미지만
 *          버린 뒤 옛 모듈 함수가 계속 불려야 한다. 고친 뒤 다시 빌드하면 리로드가 되어야 하므로 그래프는 막지 않는다.
 */
SW_TEST_CASE( ArchitectureTest, FailureBeforeTheNewImageTakesOverKeepsTheOldModule )
{
    if ( sw::FileUtil::exists( sw::modulePath( "SWGame" ) ) == false )
        SW_TEST_SKIP( "SWGame MODULE not built in this config" );
    SW_TEST_DEFENSIVE_SCOPE( "a rejected reload logs why it kept the old module" );

    SW_EXPECT_TRUE( sw::expectPreApplyFailureKeepsTheOldModule( sw::PreApplyFailure::ImageRejected ) );
    SW_EXPECT_TRUE( sw::expectPreApplyFailureKeepsTheOldModule( sw::PreApplyFailure::BatchRefused ) );
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
    if ( sw::FileUtil::exists( editorPath ) == false )
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
    const sw::Stopwatch compileStopwatch;
    while ( compiler.isCompiling() )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
        manager.update();

        const int64 elapsedSec = compileStopwatch.getElapsedMilliseconds() / 1000;
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
        sw::ModuleImageUtil::getDynamicSymbol( newHandle, "exportEditorApi" ) );
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
    // 디스크에 없는 경로를 잡는다 — 디바이스 없이는 파일을 읽지 않는다. 리소스 루트 밖의 임시 경로라 에셋 데이터베이스가
    // `.meta` 사이드카를 쓰지 않는다(루트 안의 없는 경로를 잡으면 시험을 돌릴 때마다 Resource 에 `.meta` 가 생긴다).
    const sw::string   materialPath = test::makeTempPath( "does_not_need_gpu.material" );
    sw::MaterialCache& cache        = sw::engine::getAssetManager().getMaterialManager();
    cache.clear();

    sw::Material* mat = cache.acquire( materialPath, nullptr );
    SW_EXPECT_TRUE( mat != nullptr );
    if ( mat == nullptr )
        return;

    sw::Material* again = cache.acquire( materialPath, nullptr );
    SW_EXPECT_EQUAL( mat, again );

    cache.release( materialPath );
    cache.release( materialPath );

    // 두 번 잡고 두 번 놓았으니 항목이 사라져 있어야 한다 — **참조 계수 규율은 이것이 전부다.**
    SW_EXPECT_FALSE( cache.isCached( materialPath ) );

    // 한 번 더 놓아도 아무 일이 없어야 한다(항목이 이미 없으므로 조용히 돌아간다).
    {
        test::ScopedLogSuppressor suppressor;
        cache.release( materialPath );
    }

    // 다시 잡으면 참조가 1 이므로 한 번 놓는 것으로 사라진다 — 앞의 과다 release 가 셈을 흐리지 않았다.
    SW_EXPECT_NOT_NULL( cache.acquire( materialPath, nullptr ) );
    SW_EXPECT_TRUE( cache.isCached( materialPath ) );
    cache.release( materialPath );
    SW_EXPECT_FALSE( cache.isCached( materialPath ) );

    SW_EXPECT_FALSE_MSG( sw::FileUtil::exists( materialPath + ".meta" ), "acquire wrote a .meta sidecar next to a material that does not exist" );
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
            if ( sw::FileUtil::exists( modPath ) == false )
                continue;

            void* handle = sw::ModuleImageUtil::loadDynamicLibrary( modPath );
            SW_ASSERT_NOT_NULL( handle );

            auto* getStamp = reinterpret_cast<const utf8* (*)()>(
                sw::ModuleImageUtil::getDynamicSymbol( handle, "getRHIModuleAbiStamp" ) );
            SW_ASSERT_NOT_NULL( getStamp );
            SW_EXPECT_EQUAL( sw::string( sw::kRHIModuleAbiStamp ), sw::string( getStamp() ) );

            auto* getAbiVer = reinterpret_cast<uint32 ( * )()>(
                sw::ModuleImageUtil::getDynamicSymbol( handle, "getRHIModuleAbiVersion" ) );
            SW_ASSERT_NOT_NULL( getAbiVer );
            SW_EXPECT_EQUAL( sw::kRHIModuleAbiVersion, getAbiVer() );

            auto* factory = reinterpret_cast<void* (*)()>(
                sw::ModuleImageUtil::getDynamicSymbol( handle, "createRHIDevice" ) );
            SW_ASSERT_NOT_NULL( factory );

            // 동적 언로드 및 해제
            SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( backendName, handle ) );
        }
    }
}

#endif // !SW_SHIPPING

#if defined( SW_SHIPPING )

// ------------------------------------------------------------------------------
// 2) ModuleApiTest — exportGameApi / exportEditorApi
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
        sw::ModuleImageUtil::getDynamicSymbol( handle, "exportGameApi" ) );
    SW_EXPECT_TRUE( pfnExport != nullptr );
    if ( pfnExport == nullptr )
    {
        SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", handle ) );
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

    SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", handle ) );
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
            SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "GF_Overworld", hOverworld ) );
        }
        return;
    }

    sw::engine::registerModuleTypes( "SWGame" );

    const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::ModuleImageUtil::getDynamicSymbol( handle, "exportGameApi" ) );
    SW_EXPECT_TRUE( pfnExport != nullptr );
    if ( pfnExport == nullptr )
    {
        sw::engine::unregisterModuleTypes( "SWGame" );
        if ( hOverworld != nullptr )
        {
            sw::engine::unregisterModuleTypes( "GF_Overworld" );
            SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "GF_Overworld", hOverworld ) );
        }
        SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", handle ) );
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
    SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", handle ) );

    if ( hOverworld != nullptr )
    {
        sw::engine::unregisterModuleTypes( "GF_Overworld" );
        SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "GF_Overworld", hOverworld ) );
    }
}

/**
 * @brief [ModuleApiTest] EditorModule DLL exportEditorApi
 */
SW_TEST_CASE( ModuleApiTest, ExportEditorAPI )
{
    // 전용 서버 타깃(Server)은 에디터 모듈을 짓지 않는다(매니페스트 `_listTarget: ["Client"]`).
    if ( sw::FileUtil::exists( sw::modulePath( "EditorModule" ) ) == false )
        SW_TEST_SKIP( "EditorModule not built in this config" );
    void* handle = sw::loadModule( "EditorModule" );
    SW_ASSERT_NOT_NULL( handle );

    const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
        sw::ModuleImageUtil::getDynamicSymbol( handle, "exportEditorApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );

    sw::EditorAPI api{};
    SW_EXPECT_TRUE( pfnExport( &api ) );
    SW_ASSERT_NOT_NULL( api.create );

    sw::engine::registerModuleTypes( "EditorModule" );
    sw::engine::unregisterModuleTypes( "EditorModule" );

    SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "EditorModule", handle ) );
}

/**
 * @brief [ModuleApiTest] 장르별 독립 Kit 모듈 (GF_Overworld, GF_MonsterCollector, GF_ActionCombat) 타입 등록 검증
 */
SW_TEST_CASE( ModuleApiTest, GameFrameworkKitsModuleTypeRegistration )
{
    for ( const utf8* kitName : { "GF_Overworld", "GF_MonsterCollector", "GF_ActionCombat" } )
    {
        void* handle = sw::loadModule( kitName );
        if ( handle )
        {
            sw::engine::registerModuleTypes( kitName );
            sw::engine::unregisterModuleTypes( kitName );
            SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( kitName, handle ) );
        }
    }
}

/**
 * @brief [ModuleApiTest] 자식 프로세스 역할: 공용 모듈을 먼저 올리면 GameFramework 의 타입이 제 이름으로 남는다. 그냥 실행하면 건너뛴다.
 * @details `ModuleHost` 의 순서를 그대로 밟는다 — 공용 모듈 → 키트 → SWGame 등록, SWGame 서비스 묶기(공용 모듈을 먼저 올리지 않으면 여기서
 *          GameFramework 가 지연 로드로 **처음** 올라온다), 그리고 `bindGameApi` 끝의 인자 없는 등록. 프로세스마다 한 번뿐인 일이라(이미지는 내려가지 않는다)
 *          앞선 케이스가 SWGame 을 올린 이 프로세스에서는 잴 수 없어 자식에서 잰다.
 */
SW_TEST_CASE( ModuleApiTest, SharedModuleChildKeepsItsRegistrations )
{
    if ( std::getenv( "SW_SHARED_MODULE_CHILD" ) == nullptr )
        SW_TEST_SKIP( "child only — GameFrameworkRegistersUnderItsOwnName launches it" );

    sw::LiveReloadManager manager;
    SW_ASSERT_TRUE( manager.loadSharedModule( "GameFramework" ) );
    SW_ASSERT_TRUE( manager.registerModule( "GF_Overworld", { "GameFramework" } ) );
    SW_ASSERT_TRUE( manager.registerModule( "SWGame", { "GameFramework", "GF_Overworld" } ) );

    void* const hGame = manager.getModuleHandle( "SWGame" );
    SW_ASSERT_NOT_NULL( hGame );
    const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::ModuleImageUtil::getDynamicSymbol( hGame, "exportGameApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );
    sw::GameAPI api{};
    SW_ASSERT_TRUE( pfnExport( &api ) );
    sw::ModuleService gameService{};
    if ( api.bindService != nullptr )
    {
        sw::engine::fillModuleServices( gameService, true );
        api.bindService( &gameService );
    }
    sw::engine::registerModuleTypes( "SWGame" );

    const sw::TypeInfo* pGravity = sw::engine::getTypeRegistry().findType( sw::hashed_string( "GravityComponent" ) );
    SW_ASSERT_NOT_NULL( pGravity );
    SW_EXPECT_TRUE_MSG( pGravity->_moduleName == sw::hashed_string( "GameFramework" ),
                        "GameFramework 의 타입이 다른 모듈 이름으로 등록됐습니다 — 그 모듈을 내리면 함께 지워집니다" );

    if ( api.bindService != nullptr )
        api.bindService( nullptr );
    manager.shutdown();
}

/**
 * @brief [ModuleApiTest] 공용 모듈(GameFramework)의 타입은 그것을 링크한 키트 · SWGame 이 아니라 **제 이름**으로 등록된다
 * @details 공용 모듈을 처음 부르는 쪽(Windows 지연 로드 · 리눅스 첫 키트의 DT_NEEDED)이 올리면 그 정적 등록기가 SWGame · 첫 키트의
 *          이름으로 들어가, 첫 SWGame 리로드가 GameFramework 컴포넌트를 모든 씬에서 지우고 돌아오지 않는다. 새 프로세스에서 잰다.
 */
SW_TEST_CASE( ModuleApiTest, GameFrameworkRegistersUnderItsOwnName )
{
    if ( sw::FileUtil::exists( sw::modulePath( "GameFramework" ) ) == false || sw::FileUtil::exists( sw::modulePath( "GF_Overworld" ) ) == false )
        SW_TEST_SKIP( "GameFramework · GF_Overworld 모듈이 옆에 없습니다" );

    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_SHARED_MODULE_CHILD", "1" }
    };
    const test::ChildRunResult child = test::runThisExecutableAsChild( "ModuleApiTest.SharedModuleChildKeepsItsRegistrations", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE_MSG( child._bTimedOut, ( "자식 프로세스가 시한 안에 끝나지 않았습니다 — 마지막 출력:" + child.getOutputTail() ).c_str() );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0,
                        ( "자식 프로세스에서 GameFramework 타입의 모듈 귀속 검사가 실패했습니다 — 마지막 출력:" + child.getOutputTail() ).c_str() );
}

/**
 * @brief [ModuleApiTest] 자식 프로세스 역할: 모듈을 모두 올린 뒤 등록된 모든 PROPERTY 를 직렬화기 판정으로 본다. 그냥 실행하면 건너뛴다.
 * @details `ModuleHost` 의 순서대로 공용 모듈 → 킷 → 게임 → 에디터를 올린다. 모듈마다 타입이 하나 이상 올라왔는지 먼저 본다 — 아무것도 안
 *          올라왔으면 아래 판정은 엔진 타입만 본 셈이다. 공용 모듈의 등록 귀속은 프로세스마다 한 번뿐이라(이미지는 내려가지 않는다) 앞선 케이스가
 *          모듈을 올린 이 프로세스가 아니라 자식에서 잰다(`SharedModuleChildKeepsItsRegistrations` 와 같은 까닭).
 */
SW_TEST_CASE( ModuleApiTest, ModulePropertyChildChecksEveryType )
{
    if ( std::getenv( "SW_MODULE_PROPERTY_CHILD" ) == nullptr )
        SW_TEST_SKIP( "child only — EveryModulePropertyHasATypeTheSerializersCanCarry launches it" );

    const utf8* const     arrKit[] = { "GF_Overworld", "GF_ActionCombat" };
    sw::LiveReloadManager manager;
    SW_ASSERT_TRUE( manager.loadSharedModule( "GameFramework" ) );
    sw::vector<sw::string> listGameDepend{ "GameFramework" };
    for ( const utf8* pKit : arrKit )
    {
        SW_ASSERT_TRUE( manager.registerModule( pKit, { "GameFramework" } ) );
        listGameDepend.push_back( pKit );
    }
    SW_ASSERT_TRUE( manager.registerModule( "SWGame", listGameDepend ) );
    SW_ASSERT_TRUE( manager.registerModule( "EditorModule" ) );

    sw::unordered_map<sw::hashed_string, uint32> mapModuleTypeCount;
    sw::engine::getTypeRegistry().forEachType( [&mapModuleTypeCount]( const sw::TypeInfo& info )
    { ++mapModuleTypeCount[info._moduleName]; } );
    // GF_Overworld 는 리플렉션 타입이 없는 키트다(올리기만 본다) — 타입 수는 타입을 내는 모듈만 본다.
    for ( const utf8* pModule : { "GameFramework", "GF_ActionCombat", "SWGame", "EditorModule" } )
        SW_EXPECT_TRUE_MSG( mapModuleTypeCount[sw::hashed_string( pModule )] > 0, pModule );

    const test::PropertyCarryReport report = test::makePropertyCarryReport();
    SW_EXPECT_TRUE( report._checkedCount > 100 );
    SW_EXPECT_TRUE_MSG( report._offender.empty(), report._offender.c_str() );

    manager.shutdown();
}

/**
 * @brief [ModuleApiTest] 모듈(GameFramework · 킷 · 게임 · 에디터)이 등록하는 모든 PROPERTY 도 세 형식이 실어 나를 수 있는 타입이다
 * @details 같은 판정(`ReflectionSerializationTest.EveryPropertyHasATypeTheSerializersCanCarry`)은 ReflectionTest 가 등록하는 타입만 본다 —
 *          모듈 타입의 PROPERTY 가 직렬화기가 모르는 타입이면 씬 · 프리팹 · 세이브 · 에디터 설정에 조용히 `null` 로 쓰인다. 모듈을 올리는 실행 파일이
 *          여기뿐이라 여기서 본다(자식 프로세스 — 위 케이스 머리말).
 */
SW_TEST_CASE( ModuleApiTest, EveryModulePropertyHasATypeTheSerializersCanCarry )
{
    for ( const utf8* pModule : { "GameFramework", "GF_Overworld", "GF_ActionCombat", "SWGame", "EditorModule" } )
    {
        if ( sw::FileUtil::exists( sw::modulePath( pModule ) ) == false )
            SW_TEST_SKIP( "a module is not built next to the test in this config" );
    }

    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_MODULE_PROPERTY_CHILD", "1" }
    };
    const test::ChildRunResult child = test::runThisExecutableAsChild( "ModuleApiTest.ModulePropertyChildChecksEveryType", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE_MSG( child._bTimedOut, ( "자식 프로세스가 시한 안에 끝나지 않았습니다 — 마지막 출력:" + child.getOutputTail() ).c_str() );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, ( "모듈 타입의 PROPERTY 판정이 실패했습니다 — 마지막 출력:" + child.getOutputTail( 40 ) ).c_str() );
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
            sw::ModuleImageUtil::getDynamicSymbol( handle, "exportGameApi" ) );
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
        SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", handle ) );

        if ( hOverworld != nullptr )
        {
            sw::engine::unregisterModuleTypes( "GF_Overworld" );
            SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "GF_Overworld", hOverworld ) );
        }
    }
}

/**
 * @brief [ModuleApiTest] 자식 프로세스 역할: GameFramework 를 직접 올리고 게임을 한 번 돌린 뒤 내리면, GameFramework 가 만든 이벤트 채널 항목이 디스패처에서 빠진다.
 * @details 게임이 첫 씬을 요청하면 GameFramework 의 코드가 "game" 채널에 이벤트를 낸다(`GameEventUtil::send`). 그 채널 항목의 브로드캐스트 함수와
 *          멀티캐스트의 해제자(`shared_ptr` 제어 블록)는 GameFramework 이미지의 코드라, 이미지를 내린 뒤 디스패처가 소멸하면 내려간 코드로 뛴다.
 *          공용 모듈은 프로세스마다 한 번 올라오므로(이미지는 내려가지 않는다) 깨끗한 자식에서 잰다. 그냥 실행하면 건너뛴다.
 */
SW_TEST_CASE( ModuleApiTest, UnloadChildReleasesTheChannelsItsImageCreated )
{
    if ( std::getenv( "SW_UNLOAD_CHANNEL_CHILD" ) == nullptr )
        SW_TEST_SKIP( "child only — UnloadReleasesTheChannelsTheImageCreated launches it" );

    void* const hFramework = sw::loadModule( "GameFramework" );
    SW_ASSERT_NOT_NULL( hFramework );
    sw::engine::registerModuleTypes( "GameFramework" );
    void* const hGame = sw::loadModule( "SWGame" );
    SW_ASSERT_NOT_NULL( hGame );
    sw::engine::registerModuleTypes( "SWGame" );

    const void* pFrameworkBegin{ nullptr };
    const void* pFrameworkEnd{ nullptr };
    SW_ASSERT_TRUE( sw::ModuleImageUtil::findDynamicLibraryRange( hFramework, pFrameworkBegin, pFrameworkEnd ) );

    const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::ModuleImageUtil::getDynamicSymbol( hGame, "exportGameApi" ) );
    SW_ASSERT_NOT_NULL( pfnExport );
    sw::GameAPI api{};
    SW_ASSERT_TRUE( pfnExport( &api ) );
    sw::ModuleService gameService{};
    if ( api.bindService != nullptr )
    {
        sw::engine::fillModuleServices( gameService, true );
        api.bindService( &gameService );
    }
    sw::GameHandle game = api.create();
    SW_ASSERT_NOT_NULL( game );
    SW_EXPECT_TRUE( api.initialize( game, nullptr, nullptr ) );
    for ( int32 frameIndex = 0; frameIndex < 20; ++frameIndex )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        sw::engine::getSceneManager().tickTransitions();
        api.update( game, 0.016f );
    }
    sw::engine::getTaskManager().waitAll();
    sw::engine::getSceneManager().tickTransitions();
    api.shutdown( game );
    api.destroy( game );
    if ( api.bindService != nullptr )
        api.bindService( nullptr );
    sw::engine::getSceneManager().shutdown();
    sw::engine::getSceneManager().initialize();

    sw::EventDispatcher& dispatcher = sw::engine::getEventDispatcher();
    SW_ASSERT_TRUE_MSG( dispatcher.countChannelsCreatedWithin( pFrameworkBegin, pFrameworkEnd ) > 0,
                        "GameFramework created no event channel - the case no longer exercises what it checks" );

    sw::engine::unregisterModuleTypes( "SWGame" );
    SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", hGame ) );
    sw::engine::unregisterModuleTypes( "GameFramework" );
    SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "GameFramework", hFramework ) );
    SW_EXPECT_EQUAL( 0u, dispatcher.countChannelsCreatedWithin( pFrameworkBegin, pFrameworkEnd ) );
}

/**
 * @brief [ModuleApiTest] 모듈 이미지를 내리면 그 이미지가 만든 이벤트 채널 항목도 디스패처에서 빠진다(자식 프로세스 — 위 케이스 머리말)
 */
SW_TEST_CASE( ModuleApiTest, UnloadReleasesTheChannelsTheImageCreated )
{
    if ( sw::FileUtil::exists( sw::modulePath( "GameFramework" ) ) == false || sw::FileUtil::exists( sw::modulePath( "SWGame" ) ) == false )
        SW_TEST_SKIP( "GameFramework · SWGame 모듈이 옆에 없습니다" );

    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_UNLOAD_CHANNEL_CHILD", "1" }
    };
    const test::ChildRunResult child = test::runThisExecutableAsChild( "ModuleApiTest.UnloadChildReleasesTheChannelsItsImageCreated", arrEnvironment, 60 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_EXPECT_FALSE_MSG( child._bTimedOut, ( "자식 프로세스가 시한 안에 끝나지 않았습니다 — 마지막 출력:" + child.getOutputTail() ).c_str() );
    SW_EXPECT_TRUE_MSG( child._exitCode == 0, ( "모듈을 내린 뒤 채널 항목이 남았거나 자식이 죽었습니다 — 마지막 출력:" + child.getOutputTail( 40 ) ).c_str() );
}

/**
 * @brief [ModuleApiTest] 모듈 이미지를 내려도 그 이미지가 끌어온 의존 이미지(GameFramework)는 올라와 있다
 * @details 의존 이미지의 코드(그것이 만든 이벤트 채널 항목)를 쥔 등록은 모듈을 내릴 때 뗄 수 없다 — 의존이 함께 내려갈지는 로더만 안다. 그래서
 *          `ModuleImageUtil::unloadModuleImage` 는 의존을 고정한다. Windows 는 지연 로드가 GameFramework 를 원래 잡고 있고, 리눅스는 `DT_NEEDED` 참조가
 *          함께 풀려 GameFramework 가 내려간다.
 */
SW_TEST_CASE( ModuleApiTest, UnloadKeepsTheImagesTheModulePulledIn )
{
    if ( sw::FileUtil::exists( sw::modulePath( "GameFramework" ) ) == false )
        SW_TEST_SKIP( "GameFramework 모듈이 옆에 없습니다" );

    void* const hGame = sw::loadModule( "SWGame" );
    SW_ASSERT_NOT_NULL( hGame );
    sw::engine::registerModuleTypes( "SWGame" );
    // GameFramework 의 코드를 한 번 돌려 Windows 지연 로드를 풀어 둔다 — 그래야 아래에서 여는 핸들이 새로 올리지 않고 있는 이미지를 가리킨다.
    SW_EXPECT_TRUE( sw::createAndDestroyGame( hGame ) );

    void* const hFrameworkProbe = sw::ModuleImageUtil::loadDynamicLibrary( sw::modulePath( "GameFramework" ) );
    SW_ASSERT_NOT_NULL( hFrameworkProbe );
    const void* pFrameworkBegin{ nullptr };
    const void* pFrameworkEnd{ nullptr };
    const bool  bFoundRange = sw::ModuleImageUtil::findDynamicLibraryRange( hFrameworkProbe, pFrameworkBegin, pFrameworkEnd );
    sw::ModuleImageUtil::unloadDynamicLibrary( hFrameworkProbe ); // 범위를 재느라 올린 참조만 돌려준다 — GameFramework 는 SWGame 이 끌어온 것이다
    SW_ASSERT_TRUE( bFoundRange );

    sw::engine::unregisterModuleTypes( "SWGame" );
    SW_EXPECT_TRUE( sw::ModuleImageUtil::unloadModuleImage( "SWGame", hGame ) );

    const void* pBegin{ nullptr };
    const void* pEnd{ nullptr };
    SW_EXPECT_TRUE_MSG( sw::ModuleImageUtil::findLoadedImageRange( pFrameworkBegin, pBegin, pEnd ),
                        "GameFramework went down with SWGame - the event channels it created now point at unmapped code" );
}

#endif // SW_SHIPPING
