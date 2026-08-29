#include "pch.h"

#include "App/Module/ModuleCompiler.h"

#include "Core/Common/StdHeaders.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/RHIModuleAbi.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/Module/LiveReloadManager.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "GameFramework/GameFrameworkExports.h"

#include "RuntimeAPI/ABI/EditorAPI.h"
#include "RuntimeAPI/ABI/GameAPI.h"
#include "RuntimeAPI/PluginAPI.h"
#include "RuntimeAPI/Service/GameService.h"

#include "TestFramework/TestFramework.h"

namespace
{
	void* getGameService( uint32 id )
	{
		if ( id >= sw::kModuleServiceCount )
			return nullptr;

		switch ( static_cast<sw::ModuleServiceId>( id ) )
		{
			case sw::ModuleServiceId::LocalizationManager:
				return &sw::engine::getLocalizationManager();
			case sw::ModuleServiceId::EventDispatcher:
				return &sw::engine::getEventDispatcher();
			case sw::ModuleServiceId::GlobalVariableManager:
				return &sw::engine::getGlobalVariableManager();
			case sw::ModuleServiceId::AudioSystem:
				return &sw::engine::getAudioSystem();
			case sw::ModuleServiceId::TypeRegistry:
				return &sw::engine::getTypeRegistry();
			case sw::ModuleServiceId::InputManager:
				return &sw::engine::getInputManager();
			case sw::ModuleServiceId::SceneManager:
				return &sw::engine::getSceneManager();
			case sw::ModuleServiceId::ResourceManager:
				return &sw::engine::getResourceManager();
			case sw::ModuleServiceId::DebugDrawQueue:
				return &sw::engine::getDebugDrawQueue();
			case sw::ModuleServiceId::DebugOverlayState:
				return &sw::engine::getDebugOverlayState();
			case sw::ModuleServiceId::CompressionCodecRegistry:
				return &sw::engine::getCompressionCodecRegistry();
			case sw::ModuleServiceId::ShaderCache:
				return &sw::engine::getShaderCache();
			case sw::ModuleServiceId::GameData:
				return const_cast<sw::GameData*>( &sw::engine::getGameData() );
			case sw::ModuleServiceId::ComponentDefaults:
				return &sw::engine::getComponentDefaults();
			case sw::ModuleServiceId::MonsterDataCatalog:
			case sw::ModuleServiceId::SpeciesCatalog:
			case sw::ModuleServiceId::Count:
				break;
		}
		return nullptr;
	}

	void fillGameService( sw::ModuleService& gameService )
	{
		gameService.getService = getGameService;
	}
} // namespace

#if !defined( SW_SHIPPING )

namespace sw
{
	namespace
	{
		/** @brief 테스트 모듈 DLL 경로를 만듭니다. */
		sw::string modulePath( const utf8* baseName )
		{
	#if defined( SW_TEST_MODULE_DIR )
			const sw::string dir = SW_TEST_MODULE_DIR;
	#else
			const sw::string dir = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
	#endif
			return dir + "/" + sw::FileUtil::formatSharedLibraryName( baseName );
		}

		/** @brief 테스트 모듈을 동적 로드합니다. */
		void* loadModule( const utf8* name )
		{
			const sw::string path = modulePath( name );
			return sw::FileUtil::loadDynamicLibrary( path );
		}

	} // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) Architecture — RHI ABI·핫리로드 keep-old
// ------------------------------------------------------------------------------
/**
 * @brief [Architecture] 모든 RHI 백엔드 모듈 (DX11, DX12, Vulkan, GL) ABI 스탬프 및 팩토리 export 검증
 */
SW_TEST_CASE( Architecture, AllRHIModulesAbiStampExports )
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
 * @brief [Architecture] 원본 없으면 LiveReload 가 이전 모듈을 유지
 */
SW_TEST_CASE( Architecture, LiveReloadKeepOldOnMissingOriginal )
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
SW_TEST_CASE( Architecture, LiveReloadPoisonIgnoresTrigger )
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
SW_TEST_CASE( Architecture, LiveReloadOnAfterPoisonFailsRegister )
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
SW_TEST_CASE( Architecture, LiveReloadCascadeAbortsAfterOnAfterPoison )
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
 * @brief [Architecture] 단일 모듈 LiveReload 정상 성공 및 섀도 핸들 갱신 검증 (Happy Path)
 */
SW_TEST_CASE( Architecture, LiveReloadSuccessfulShadowReload )
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
		SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled, &newHandleInCb]( void* h )
	{
		onAfterCalled = true;
		newHandleInCb = h;
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
 * @brief [Architecture] 종속 모듈 간 캐스케이드 LiveReload 순차 성공 검증 (GameFramework -> SWGame)
 */
SW_TEST_CASE( Architecture, LiveReloadCascadeSuccessPath )
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
 * @brief [Architecture] EditorModule DLL 독립 LiveReload 및 C-ABI 테이블 재바인딩 검증
 */
SW_TEST_CASE( Architecture, LiveReloadEditorModule )
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
		SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled, &newHandle]( void* h )
	{
		onAfterCalled = true;
		newHandle	  = h;
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

	// 새로 로드된 모듈에서 C-ABI exportEditorAPI 정상 동작 검증
	const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
		sw::FileUtil::getDynamicSymbol( newHandle, "exportEditorAPI" ) );
	SW_ASSERT_NOT_NULL( pfnExport );

	sw::EditorAPI api{};
	SW_EXPECT_TRUE( pfnExport( &api ) );
	SW_EXPECT_TRUE( api.create != nullptr );
	SW_EXPECT_TRUE( api.render != nullptr );

	manager.shutdown();
}

/**
 * @brief [Architecture] 장르 키트 모듈 (GF_Overworld, GF_TurnBattle, GF_ActionCombat) 개별 LiveReload 검증
 */
SW_TEST_CASE( Architecture, LiveReloadGenreKitsIndividuallyAndCascaded )
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
			SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&reloaded, &newH]( void* h )
		{
			reloaded = true;
			newH	 = h;
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
 * @brief [Architecture] 풀스택 복합 의존 그래프 (GameFramework + GF_Overworld + SWGame + EditorModule) 동시 및 캐스케이드 LiveReload
 */
SW_TEST_CASE( Architecture, MultiModuleFullStackLiveReload )
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
 * @brief [Architecture] ModuleCompiler (CMake 백그라운드 컴파일) -> LiveReloadManager (DLL 핫스왑) End-to-End 전체 파이프라인 검증
 */
SW_TEST_CASE( Architecture, ModuleCompilerAndLiveReloadE2E )
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
		SW_DELEGATE_LAMBDA( sw::LiveReloadManager::OnAfterReloadDelegate, [&onAfterCalled, &newHandle]( void* h )
	{
		onAfterCalled = true;
		newHandle	  = h;
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
	for ( int32 stepIndex = 0; stepIndex < 50 && onAfterCalled == false; ++stepIndex )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
		manager.update();
	}

	SW_EXPECT_FALSE( compiler.isCompiling() );
	SW_EXPECT_EQUAL( static_cast<int32>( sw::BuildState::Success ), static_cast<int32>( compiler.getBuildState() ) );
	SW_EXPECT_EQUAL( 0, compiler.getLastExitCode() );

	// 5) 컴파일 완료 후 LiveReloadManager가 모듈 핫스왑 콜백을 정상 실행했는지 검증
	SW_EXPECT_TRUE( onBeforeCalled );
	SW_EXPECT_TRUE( onAfterCalled );
	SW_ASSERT_NOT_NULL( newHandle );

	// 6) 새로 핫스왑된 모듈에서 C-ABI exportEditorAPI 심볼 및 함수 테이블 유효성 검증
	const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
		sw::FileUtil::getDynamicSymbol( newHandle, "exportEditorAPI" ) );
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
 * @brief [Architecture] GPU 없이 MaterialCache acquire/release
 */
SW_TEST_CASE( Architecture, MaterialCacheAcquireReleaseNoGpu )
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
	cache.clear();
}

/**
 * @brief [Architecture] 4대 RHI 그래픽스 백엔드 (DX11, DX12, Vulkan, GL) 런타임 동적 스왑 및 연속 리로드 검증
 */
SW_TEST_CASE( Architecture, RHIBackendDynamicSwapAndReload )
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
// 2) ModuleAPI — exportGameAPI / exportEditorAPI
// ------------------------------------------------------------------------------
/**
 * @brief [ModuleAPI] Shipping 정적 exportGameAPI
 */
SW_TEST_CASE( ModuleAPI, ExportGameAPI_ShippingStatic )
{
	sw::GameAPI api{};
	SW_EXPECT_TRUE( exportGameAPI( &api ) );
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
 * @brief [ModuleAPI] SWGame DLL exportGameAPI
 */
SW_TEST_CASE( ModuleAPI, ExportGameAPI )
{
	void* handle = sw::loadModule( "SWGame" );
	SW_EXPECT_TRUE( handle != nullptr );
	if ( handle == nullptr )
		return;

	const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>(
		sw::FileUtil::getDynamicSymbol( handle, "exportGameAPI" ) );
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
 * @brief [ModuleAPI] Full Game Scene, Component Lifecycle & Tick Verification
 */
SW_TEST_CASE( ModuleAPI, FullGameSceneAndComponentLifecycle )
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

	const sw::PFN_ExportGameAPI pfnExport = reinterpret_cast<sw::PFN_ExportGameAPI>( sw::FileUtil::getDynamicSymbol( handle, "exportGameAPI" ) );
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
		fillGameService( gameService );
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

	auto waitForSceneLoad = [&]( const utf8* path ) -> sw::Scene*
	{
		auto pathMatches = []( const sw::string& a, const utf8* b ) -> bool
		{
			if ( b == nullptr || a.empty() )
				return false;
			const sw::string lowerA = sw::StringUtil::toLower( a.c_str() );
			const sw::string lowerB = sw::StringUtil::toLower( b );
			return lowerA.find( lowerB ) != sw::string::npos || lowerB.find( lowerA ) != sw::string::npos;
		};

		if ( sw::engine::areEngineServicesBound() )
			sw::engine::getTaskManager().waitAll();
		sw::engine::getSceneManager().tickTransitions();

		sw::Scene* current = sw::engine::getSceneManager().getActiveScene();
		if ( current == nullptr || pathMatches( current->getSourcePath(), path ) == false )
		{
			sw::engine::getSceneManager().requestLoadAsync( path );
		}

		for ( int32 iter = 0; iter < 100; ++iter )
		{
			if ( sw::engine::areEngineServicesBound() )
				sw::engine::getTaskManager().waitAll();
			sw::engine::getSceneManager().tickTransitions();

			sw::Scene* sc = sw::engine::getSceneManager().getActiveScene();
			if ( sc != nullptr && sc->getObjectManager() && sc->getObjectManager()->getAllGameObjects().size() > 0 )
			{
				if ( pathMatches( sc->getSourcePath(), path ) )
				{
					return sc;
				}
			}
			std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
		}
		sw::engine::getSceneManager().tickTransitions();
		return sw::engine::getSceneManager().getActiveScene();
	};

	// 2) Load converted 0.title.scene.xml
	sw::Scene* titleScene = waitForSceneLoad( "game/demo/maps/0.title.scene.xml" );
	SW_ASSERT_NOT_NULL( titleScene );
	SW_EXPECT_TRUE( titleScene->getObjectManager()->getAllGameObjects().size() > 0 );

	for ( int32 frameIndex = 0; frameIndex < 5; ++frameIndex )
	{
		api.update( game, 0.016f );
		sw::Scene* pActive = sw::engine::getSceneManager().getActiveScene();
		if ( pActive != nullptr )
			pActive->tick( 0.016f );
	}

	// 3) Load converted 1.entrance.scene.xml
	sw::engine::getSceneManager().requestLoadAsync( "game/demo/maps/1.entrance.scene.xml" );
	sw::Scene* entranceScene = waitForSceneLoad( "game/demo/maps/1.entrance.scene.xml" );
	SW_ASSERT_NOT_NULL( entranceScene );
	SW_EXPECT_TRUE( entranceScene->getObjectManager()->getAllGameObjects().size() > 0 );

	for ( int32 frameIndex = 0; frameIndex < 5; ++frameIndex )
	{
		api.update( game, 0.016f );
		sw::Scene* pActive = sw::engine::getSceneManager().getActiveScene();
		if ( pActive != nullptr )
			pActive->tick( 0.016f );
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

/**
 * @brief [ModuleAPI] EditorModule DLL exportEditorAPI
 */
SW_TEST_CASE( ModuleAPI, ExportEditorAPI )
{
	void* handle = sw::loadModule( "EditorModule" );
	SW_ASSERT_NOT_NULL( handle );

	const sw::PFN_ExportEditorAPI pfnExport = reinterpret_cast<sw::PFN_ExportEditorAPI>(
		sw::FileUtil::getDynamicSymbol( handle, "exportEditorAPI" ) );
	SW_ASSERT_NOT_NULL( pfnExport );

	sw::EditorAPI api{};
	SW_EXPECT_TRUE( pfnExport( &api ) );
	SW_ASSERT_NOT_NULL( api.create );

	sw::engine::registerModuleTypes( "EditorModule" );
	sw::engine::unregisterModuleTypes( "EditorModule" );

	sw::FileUtil::unloadDynamicLibrary( handle );
}

/**
 * @brief [ModuleAPI] 장르별 독립 Kit 모듈 (GF_Overworld, GF_TurnBattle, GF_ActionCombat) 타입 등록 검증
 */
SW_TEST_CASE( ModuleAPI, GameFrameworkKitsModuleTypeRegistration )
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
 * @brief [ModuleAPI] SWGame 모듈 반복 로드/언로드 사이클 안정성
 */
SW_TEST_CASE( ModuleAPI, GameModuleRepeatedReloadCycle )
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
			sw::FileUtil::getDynamicSymbol( handle, "exportGameAPI" ) );
		SW_ASSERT_NOT_NULL( pfnExport );

		sw::GameAPI api{};
		SW_EXPECT_TRUE( pfnExport( &api ) );

		if ( api.bindService )
		{
			sw::ModuleService gameService{};
			fillGameService( gameService );
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
