#include "pch.h"

#include "ModuleHost/ModuleHost.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Renderer/RenderThread.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/Window/IWindow.h"

#include "ModuleHost/LiveReloadManager.h"
#include "ModuleHost/ModuleCompiler.h"

#include "RuntimeAPI/ABI/ModuleAbi.h"
#include "RuntimeAPI/Service/ModuleService.h"
#include "RuntimeAPI/Service/ServiceListColumns.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct ModuleHostInternal
        {
            enum class Target : uint8
            {
                Editor,
                Game
            };

            /**
             * @brief 모듈이 호스트와 **같은 테이블 구조**로 빌드됐는지 대조합니다.
             * @details `GameAPI` · `EditorAPI` 는 함수 포인터를 순서대로 늘어놓은 구조체입니다. 모듈은 자기가 아는 자리에 채우고 호스트는
             *          자기가 아는 자리에서 읽으므로, 서로 다른 헤더로 빌드되면 **호스트가 엉뚱한 함수를 부릅니다.** 아래의
             *          `create != nullptr && destroy != nullptr` 만으로는 막지 못합니다 — 그 둘은 **맨 앞**에 있어서 가운데에 끼워 넣어도
             *          채워집니다. 핫 리로드는 모듈만 다시 빌드하는 기능이라 이런 어긋남이 생기는 바로 그 상황입니다. RHI 경계의
             *          `RHIModuleAbi.h` 와 같은 대조입니다.
             */
            static bool matchesModuleAbi( void* pLibraryModule, const utf8* pVersionSymbol, const utf8* pStampSymbol,
                                          const utf8* pModuleName )
            {
                const PFN_GetModuleAbiVersion pfnVersion =
                    reinterpret_cast<PFN_GetModuleAbiVersion>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, pVersionSymbol ) );
                if ( pfnVersion == nullptr || pfnVersion() != kModuleAbiVersion )
                {
                    SW_LOG_ERROR( "%# 모듈 ABI 버전이 다릅니다 (기대 %#) — 엔진과 모듈을 함께 다시 빌드하세요.",
                                  pModuleName, kModuleAbiVersion );
                    return false;
                }

                const PFN_GetModuleAbiStamp pfnStamp =
                    reinterpret_cast<PFN_GetModuleAbiStamp>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, pStampSymbol ) );
                if ( pfnStamp == nullptr || StringUtil::equals( pfnStamp(), kModuleAbiStamp ) == false )
                {
                    SW_LOG_ERROR( "%# 모듈 ABI 스탬프가 다릅니다 (기대 '%#') — 엔진과 모듈을 함께 다시 빌드하세요.",
                                  pModuleName, kModuleAbiStamp );
                    return false;
                }
                return true;
            }

            /** @brief 모듈 이미지에서 API 표를 받는 데 쓰는 심볼 이름입니다. 에디터 · 게임이 같은 절차를 이 이름만 바꿔 씁니다. */
            struct ModuleApiSymbols
            {
                const utf8* _pVersionSymbol;
                const utf8* _pStampSymbol;
                const utf8* _pExportSymbol;
                const utf8* _pModuleLabel;
            };
            static constexpr ModuleApiSymbols kEditorSymbols{ "getEditorModuleAbiVersion", "getEditorModuleAbiStamp", "exportEditorApi", "Editor" };
            static constexpr ModuleApiSymbols kGameSymbols{ "getGameModuleAbiVersion", "getGameModuleAbiStamp", "exportGameApi", "Game" };

            /**
             * @brief 모듈 이미지의 ABI 를 대조하고 API 표를 받습니다(에디터 · 게임 공통). 받지 못하면 @p outApi 는 빈 표입니다.
             * @details 리로드 전 검사(`is*ImageUsable`)와 바인딩(`bind*Api`)이 같은 절차를 씁니다.
             */
            template <typename TApi, typename TExportFn>
            [[nodiscard]] static bool exportApiFromImage( void* pLibraryModule, const ModuleApiSymbols& symbols, TApi& outApi )
            {
                outApi = {};
                if ( pLibraryModule == nullptr ||
                     matchesModuleAbi( pLibraryModule, symbols._pVersionSymbol, symbols._pStampSymbol, symbols._pModuleLabel ) == false )
                    return false;
                const TExportFn pfnExport = reinterpret_cast<TExportFn>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, symbols._pExportSymbol ) );
                if ( pfnExport == nullptr || pfnExport( &outApi ) == false )
                {
                    outApi = {};
                    SW_LOG_ERROR( "The %# module does not export its API table (%#)", symbols._pModuleLabel, symbols._pExportSymbol );
                    return false;
                }
                return true;
            }

            /**
             * @brief 바인딩한 API 표로 인스턴스를 만들고 초기화합니다(에디터 · 게임 공통). 실패하면 만든 것을 부수고 핸들을 비웁니다.
             * @note @p bRequireDevice 면 **디바이스가 없을 때 만들지 않습니다.** `RHI::getDevice()` 는 널 참조를 반환하므로 묻는 것 자체가 죽는
             *       길이고, 만들어 봐야 초기화가 실패할 것이 정해져 있습니다. 전용 서버의 게임은 디바이스 없이(nullptr) 만듭니다.
             */
            template <typename TApi>
            [[nodiscard]] static bool createInstance( const TApi& api, void*& pOutHandle, IWindow* pWindow, RHI* pRHI, bool bRequireDevice,
                                                      const utf8* pModuleLabel )
            {
                pOutHandle            = nullptr;
                const bool bHasDevice = pRHI != nullptr && pRHI->hasDevice();
                if ( bRequireDevice && bHasDevice == false )
                {
                    SW_LOG_ERROR( "No RHI device - %# instance is not created", pModuleLabel );
                    return false;
                }

                pOutHandle = api.create();
                if ( pOutHandle == nullptr )
                {
                    SW_LOG_ERROR( "Failed to create %# instance", pModuleLabel );
                    return false;
                }

                if ( api.initialize( pOutHandle, pWindow, bHasDevice ? &pRHI->getDevice() : nullptr ) == false )
                {
                    SW_LOG_ERROR( "Failed to initialize %# instance", pModuleLabel );
                    if ( api.destroy != nullptr )
                        api.destroy( pOutHandle );
                    pOutHandle = nullptr;
                    return false;
                }
                SW_LOG_INFO( "Module instance initialized: %#", pModuleLabel );
                return true;
            }

            /**
             * @brief 인스턴스를 내립니다(에디터 · 게임 공통): shutdown → destroy → (표를 놓으면) 모듈 타입 등록 해제 → 서비스 떼기 → 핸들 비우기
             *        → (표를 놓으면) API 표 비우기.
             * @details 타입 등록 해제가 그 모듈의 살아 있는 컴포넌트를 지웁니다. 그 소멸자는 모듈 코드라 서비스가 아직 붙어 있는 동안이어야 합니다.
             */
            template <typename TApi>
            static void destroyInstance( TApi& inoutApi, void*& pInOutHandle, bool bReleaseApiTable, [[maybe_unused]] const utf8* pModuleName )
            {
                if ( pInOutHandle != nullptr && inoutApi.shutdown != nullptr )
                    inoutApi.shutdown( pInOutHandle );
                if ( pInOutHandle != nullptr && inoutApi.destroy != nullptr )
                    inoutApi.destroy( pInOutHandle );
                // Shipping 은 모듈을 내리지 않으므로 등록 해제 자체가 없다(Engine 에도 그 코드가 없다).
#if !defined( SW_SHIPPING )
                if ( bReleaseApiTable )
                    engine::unregisterModuleTypes( pModuleName );
#endif
                if ( inoutApi.bindService != nullptr )
                    inoutApi.bindService( nullptr );
                pInOutHandle = nullptr;
                if ( bReleaseApiTable )
                    inoutApi = {};
            }

            /** @brief 호스트가 제공하는 서비스 테이블을 만듭니다. 게임 모듈에는 GameVisible 인 것만 노출합니다. */
            template <Target TargetModule>
            static void fillModuleService( const ModuleHost* pHost, ModuleService& outService )
            {
                engine::fillModuleServices( outService, TargetModule == Target::Game );

#define SW_HOST_SERVICE( member, Tag, Type, getter, visibility )                                         \
    if constexpr ( TargetModule == Target::Editor || ( SW_SERVICE_IS_GAME_VISIBLE( visibility ) == 1 ) ) \
    {                                                                                                    \
        outService.arrServices[internal::toRawServiceId( internal::ModuleServiceId::Type )] =            \
            ( pHost != nullptr ) ? pHost->getter() : nullptr;                                            \
    }

#include "RuntimeAPI/Service/HostServiceList.xxx"
#undef SW_HOST_SERVICE
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ModuleHost" );

    ModuleHost::ModuleHost()
        : _moduleCompiler{ nullptr }
        , _editorApi{}
        , _gameApi{}
        , _editor{ nullptr }
        , _game{ nullptr }
        , _pLiveReloadManager{ nullptr }
        , _pRHI{ nullptr }
        , _pWindow{ nullptr }
        , _pRenderThread{ nullptr }
        , _listGameSavedState{}
        , _frameState{}
        , _bEnableEditor{ SW_FALSE }
        , _bEditorModuleActive{ SW_TRUE }
        , _bDedicatedServer{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ModuleHost::~ModuleHost()
    {
        shutdown();
    }

    bool ModuleHost::loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
    {
        _pLiveReloadManager  = pLiveReloadManager;
        _bEditorModuleActive = resolution.isActive( config::kTargetEditorModule ) ? SW_TRUE : SW_FALSE;
#if defined( SW_SHIPPING )
        // 게임 · 키트 · GameFramework 는 정적 링크라 올릴 이미지가 없다 — 그 타입은 리플렉션 단계가 이미 모았고, 무엇을 링크할지는 CMake 가 같은 매니페스트로 정했다.
        (void)catalog;
        return true;
#else
        if ( _pLiveReloadManager == nullptr )
            return true;

        SW_MEMORY_SCOPE( Game );
        vector<string> listGameModule;

        // 적재 순서는 매니페스트의 의존이 정한다(의존이 먼저, 동점은 이름 순). 공용 모듈(GameFramework)은 그것을 링크하는 키트 · SWGame 보다 먼저
        // 오므로 제 이름으로 등록된다(`LiveReloadManager::loadSharedModule` 의 이유).
        for ( const string& moduleName : resolution._listLoadOrder )
        {
            const ModuleManifest* pManifest = catalog.findManifest( moduleName );
            if ( pManifest == nullptr )
                continue;
            if ( pManifest->_kind == ModuleKind::GameFramework )
            {
                if ( _pLiveReloadManager->loadSharedModule( moduleName ) == false )
                {
                    SW_LOG_ERROR( "Shared module load failed (%#)", moduleName );
                    return false;
                }
                listGameModule.push_back( moduleName );
                continue;
            }
            if ( pManifest->_kind != ModuleKind::Kit )
                continue;

            vector<string> listDependency;
            for ( const ModuleDependency& dependency : pManifest->_listDependency )
            {
                listDependency.push_back( dependency._name );
            }
            if ( _pLiveReloadManager->registerModule( moduleName, listDependency ) == false )
            {
                SW_LOG_ERROR( "Kit module register failed (%#)", moduleName );
                return false;
            }
            _pLiveReloadManager->setOnBeforeReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameplayDllReload, this ) );
            _pLiveReloadManager->setOnAfterReload( moduleName, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameplayDllReload, this ) );
            listGameModule.push_back( moduleName );
        }

        // 게임 모듈은 이미지만 올린다(타입 등록까지). 리로드 직후 콜백 — 인스턴스 생성 — 은 RHI 가 선 뒤 `initialize` 가 걸고 부른다.
        _pLiveReloadManager->setOnBeforeReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeGameReload, this ) );
        _pLiveReloadManager->setOnReloadFault( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &ModuleHost::onGameReloadFault, this ) );
        _pLiveReloadManager->setOnValidateImage( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnValidateImageDelegate, &ModuleHost::isGameImageUsable, this ) );
        if ( _pLiveReloadManager->registerModule( sw::config::kTargetGameModule, listGameModule ) == false )
        {
            SW_LOG_ERROR( "SWGame module register failed" );
            return false;
        }

        if ( _pLiveReloadManager->isGraphBroken() )
        {
            SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
            return false;
        }

        _pLiveReloadManager->setOnBeforeCommitBatch(
            SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeCommitBatchDelegate, &ModuleHost::onBeforeCommitBatch, this ) );
        return true;
#endif
    }

    bool ModuleHost::initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor )
    {
        _pLiveReloadManager = pLiveReloadManager;
        _pRHI               = pRHI;
        _pWindow            = pWindow;
        _pRenderThread      = pRenderThread;
        _bEnableEditor      = bEnableEditor ? SW_TRUE : SW_FALSE;

#if !defined( SW_SHIPPING )
        _moduleCompiler = make_unique<ModuleCompiler>( _pLiveReloadManager );
        if ( _pLiveReloadManager != nullptr )
        {
            _pLiveReloadManager->setDrainWorkers(
                SW_DELEGATE_METHOD( LiveReloadManager::DrainWorkersDelegate, &ModuleHost::drainRenderWorkers, this ) );
        }
#endif

        // 게임이 먼저, 에디터가 나중이다. 게임은 처음 여는 씬을 요청하고(`GameInstanceBase::requestFirstScene`) 에디터는 제 시작 씬
        // (`-gv_editorStartupScene`)을 요청한다. 씬 매니저는 마지막 요청을 남기므로 나중에 요청한 에디터의 씬이 열린다(`GameSettings::_startMap` 주석).
#if defined( SW_SHIPPING )
        onAfterGameReload( nullptr );
#else
        if ( _pLiveReloadManager != nullptr )
        {
            BLOCK( "SWGame 인스턴스 생성" )
            {
                SW_MEMORY_SCOPE( Game );
                _pLiveReloadManager->setOnAfterReload( sw::config::kTargetGameModule, SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterGameReload, this ) );
                if ( _pLiveReloadManager->runAfterReload( sw::config::kTargetGameModule ) == false )
                {
                    SW_LOG_ERROR( "SWGame instance could not be started" );
                    return false;
                }
            }
        }

    #if !defined( SW_SHIPPING )
        if ( _bEnableEditor == SW_TRUE && _bEditorModuleActive == SW_FALSE )
        {
            // -EnableEditor 를 줬는데 매니페스트가 에디터 모듈을 껐다(프로젝트 · 구성) — 에디터 없이 조용히 뜨지 않는다.
            SW_LOG_ERROR( "-EnableEditor was given, but the module manifests turn %# off for this project", config::kTargetEditorModule );
            return false;
        }
    #endif

        if ( _bEnableEditor == SW_TRUE && _pLiveReloadManager != nullptr )
        {
            BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
            {
                SW_MEMORY_SCOPE( Editor );
                _pLiveReloadManager->setOnBeforeReload( config::kTargetEditorModule,
                                                        SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &ModuleHost::onBeforeEditorReload, this ) );
                _pLiveReloadManager->setOnAfterReload( config::kTargetEditorModule,
                                                       SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &ModuleHost::onAfterEditorReload, this ) );
                _pLiveReloadManager->setOnReloadFault( config::kTargetEditorModule,
                                                       SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &ModuleHost::onEditorReloadFault, this ) );
                _pLiveReloadManager->setOnValidateImage( config::kTargetEditorModule,
                                                         SW_DELEGATE_METHOD( LiveReloadManager::OnValidateImageDelegate, &ModuleHost::isEditorImageUsable, this ) );
                if ( _pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
                {
                    SW_LOG_ERROR( "Editor Module 로드에 실패했습니다." );
                    return false;
                }

                if ( _pLiveReloadManager->isGraphBroken() )
                {
                    SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
                    return false;
                }
            }
        }
#endif

        return true;
    }

    bool ModuleHost::initializeDedicatedServer( LiveReloadManager* pLiveReloadManager )
    {
        _bDedicatedServer = SW_TRUE;
        return initialize( pLiveReloadManager, nullptr, nullptr, nullptr, false );
    }

    void ModuleHost::shutdown()
    {
        if ( _moduleCompiler != nullptr )
        {
            _moduleCompiler->shutdown();
            _moduleCompiler.reset();
        }

        // 에디터 · 게임을 한 번의 비우기로 내린다 — onBefore*Reload 를 따로 부르면 drainRenderWorkers 가 두 번 돌아 종료 경로에서
        // 태스크 대기 제한 시간을 두 번까지 기다릴 수 있다.
        suspendModules( ModuleScope::Both, true );

#if !defined( SW_SHIPPING )
        // 콜백은 ModuleHost 의 메서드를 가리킨다. 이 객체가 사라지기 전에 떼어 낸다.
        //
        // **모듈마다 건 것까지 뗀다.** `setOnBeforeReload`/`setOnAfterReload` 로 모듈마다 건 델리게이트도 이 객체의 메서드를
        // 가리킨다. `App` 은 ModuleHost 를 먼저 지우고 나중에 LiveReloadManager 를 내리므로, 그 사이에 리로드가 한 번 돌면 이미
        // 사라진 객체를 부르게 된다. 이름을 여기에 다시 적지 않으려고 등록부 쪽에 창구를 두었다(키트 모듈은 설정에서 오므로 이
        // 자리에서는 이름을 알 수도 없다).
        if ( _pLiveReloadManager != nullptr )
        {
            _pLiveReloadManager->setDrainWorkers( {} );
            _pLiveReloadManager->setOnBeforeCommitBatch( {} );
            _pLiveReloadManager->clearReloadCallbacks();
        }
#endif
    }

    bool ModuleHost::importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly )
    {
#if defined( SW_SHIPPING )
        (void)kind;
        (void)bCheckOnly;
        SW_LOG_ERROR( "Asset importing needs the editor module, which a Shipping build does not have - run it from a Dev build." );
        return false;
#else
        const string modulePath     = ModuleImageUtil::findModuleLibraryPath( sw::config::kTargetEditorModule );
        void* const  pLibraryModule = FileUtil::exists( modulePath ) ? ModuleImageUtil::loadDynamicLibrary( modulePath ) : nullptr;
        if ( pLibraryModule == nullptr )
        {
            SW_LOG_ERROR( "Asset importing needs the editor module in Bin/Modules: %#", modulePath.c_str() );
            return false;
        }
        (void)ModuleImageUtil::bindDelayLoadImports( pLibraryModule ); // 못 묶으면 경고했다

        // 올리는 순간 모듈의 정적 등록기가 전역 머리에 매달린다. 모듈 이름으로 등록해 두어야 내리기 전에 걷을 수 있다.
        engine::registerModuleTypes( sw::config::kTargetEditorModule );

        bool bSucceeded = false;
        if ( ModuleHostInternal::matchesModuleAbi( pLibraryModule, ModuleHostInternal::kEditorSymbols._pVersionSymbol,
                                                   ModuleHostInternal::kEditorSymbols._pStampSymbol, ModuleHostInternal::kEditorSymbols._pModuleLabel ) )
        {
            const PFN_ImportEditorAssets pfnImport =
                reinterpret_cast<PFN_ImportEditorAssets>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, kImportEditorAssetsSymbol ) );
            if ( pfnImport == nullptr )
                SW_LOG_ERROR( "The editor module does not export %#", kImportEditorAssetsSymbol );
            else
                bSucceeded = pfnImport( static_cast<uint32>( kind ), bCheckOnly ? 1u : 0u ) == 0;
        }

        engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
        // 내리지 못하면(다른 코드가 아직 그 이미지의 이벤트 채널을 구독한다) 프로세스 끝까지 올라와 있을 뿐이다 — 이유는 경고로 남는다.
        (void)ModuleImageUtil::unloadModuleImage( sw::config::kTargetEditorModule, pLibraryModule );
        return bSucceeded;
#endif
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    bool ModuleHost::queryGameplayActive() const
    {
        if ( hasEditor() == false || _editorApi.isPlaying == nullptr )
            return true;
        return _editorApi.isPlaying( _editor );
    }

    bool ModuleHost::queryTickScene() const
    {
        if ( hasEditor() == false )
            return true;
        if ( _editorApi.isPaused != nullptr && _editorApi.isPaused( _editor ) )
        {
            if ( _editorApi.isPlaying == nullptr || _editorApi.isPlaying( _editor ) == false )
                return false;
        }
        return true;
    }

    void ModuleHost::sampleGameViewport()
    {
        _frameState._gameViewportTarget = 0;
        _frameState._gameViewportWidth  = 0;
        _frameState._gameViewportHeight = 0;
        if ( hasEditor() == false || _editorApi.getGameViewport == nullptr )
            return;

        _editorApi.getGameViewport( _editor,
                                    &_frameState._gameViewportTarget,
                                    &_frameState._gameViewportWidth,
                                    &_frameState._gameViewportHeight );
    }

    void ModuleHost::beginFrame()
    {
        _frameState                  = ModuleFrameState{};
        _frameState._bGameplayActive = queryGameplayActive() ? SW_TRUE : SW_FALSE;
        // 에디터가 없으면 월드는 처음부터 플레이 중이다(에디터가 있으면 Play · Stop 이 정한다) — 켜지 않으면 onBeginPlay 가 불리지 않는다.
        // 이미 켜져 있으면 아무 일도 없다.
        if ( hasEditor() == false && engine::getSceneManager().isWorldPlaying() == false )
            engine::getSceneManager().setWorldPlaying( true );
        // 게임 update 가 돌지 않는 동안(에디터 멈춤 · Simulate)은 게임이 연 화면(로딩 · HUD · 메뉴)을 그리지 않는다 — 그 화면을 닫을 게임 코드가
        // 돌지 않으므로, 그리면 첫 Play 전까지 로딩 화면이 게임 뷰를 덮는다. 오프스크린 미리보기(UI Preview)는 그대로다.
        if ( engine::areEngineServicesBound() )
            engine::getUiSystem().setOnScreenSuppressed( _frameState._bGameplayActive == SW_FALSE );
    }

    void ModuleHost::updateGame( float32 deltaTime )
    {
        SW_MEMORY_SCOPE( Game );
        if ( _game != nullptr && _gameApi.update != nullptr && _frameState._bGameplayActive == SW_TRUE )
            _gameApi.update( _game, deltaTime );
    }

    void ModuleHost::fixedUpdateGame( float32 fixedDeltaTime )
    {
        SW_MEMORY_SCOPE( Game );
        if ( _game != nullptr && _gameApi.fixedUpdate != nullptr && _frameState._bGameplayActive == SW_TRUE )
            _gameApi.fixedUpdate( _game, fixedDeltaTime );
    }

    void ModuleHost::updateEditorUi( float32 /*deltaTime*/ )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false )
            return;

        if ( _editorApi.updateUi != nullptr )
            _editorApi.updateUi( _editor );

        // 에디터가 이번 프레임 입력을 처리한 **뒤에** 확정한다. Step 버튼은 이 갱신에서 눌리고, 씬을 한 칸 틱한 다음
        // endEditorFrame 에서 소비된다. 이 질의를 프레임 앞으로 옮기면 Step 이 틱 없이 소비되어 아무 일도 일어나지 않는다.
        _frameState._bTickScene = queryTickScene() ? SW_TRUE : SW_FALSE;
        sampleGameViewport();
    }

    void ModuleHost::endEditorFrame()
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false || _editorApi.endFrame == nullptr )
            return;
        _editorApi.endFrame( _editor );
    }

    bool ModuleHost::onWindowMessage( const NativeWindowEvent& event )
    {
        if ( hasEditor() == false || _editorApi.processEvent == nullptr )
            return false;

        // 에디터 내부 상태 갱신과 입력 필터링은 에디터 모듈 안에서 처리한다
        return _editorApi.processEvent( _editor, &event );
    }

    CameraComponent* ModuleHost::getViewportCamera() const
    {
        if ( hasEditor() == false || _editorApi.getViewportCamera == nullptr )
            return nullptr;
        return static_cast<CameraComponent*>( _editorApi.getViewportCamera( _editor ) );
    }

    // ======================================================================
    // LiveReload 콜백 — 에디터
    // ======================================================================

    void ModuleHost::onBeforeEditorReload()
    {
        suspendModules( ModuleScope::Editor, true );
    }

    void ModuleHost::onAfterEditorReload( void* pLibraryModule )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( bindEditorApi( pLibraryModule ) == false )
        {
            markReloadGraphBroken( "EditorAPI bind failed after reload" );
            return;
        }

        if ( createEditorInstance() == false )
        {
            _editorApi = {};
            markReloadGraphBroken( "Editor create/initialize failed after reload" );
            return;
        }
    }

    // ======================================================================
    // LiveReload 콜백 — 게임
    // ======================================================================

    void ModuleHost::onBeforeGameReload()
    {
        suspendModules( ModuleScope::Game, true );
    }

    void ModuleHost::onAfterGameReload( void* pLibraryModule )
    {
        SW_MEMORY_SCOPE( Game );
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( _gameApi.create == nullptr && bindGameApi( nullptr ) == false )
            return;
#else
        void* pModuleHandle = pLibraryModule;
        if ( pModuleHandle == nullptr && _pLiveReloadManager != nullptr )
            pModuleHandle = _pLiveReloadManager->getModuleHandle( sw::config::kTargetGameModule );

        if ( bindGameApi( pModuleHandle ) == false )
        {
            markReloadGraphBroken( "GameAPI bind failed after reload" );
            return;
        }
#endif

        if ( createGameInstance() == false )
        {
            _gameApi = {};
            markReloadGraphBroken( "Game create/initialize failed after reload" );
            return;
        }

        restoreGameState();
    }

    // ======================================================================
    // LiveReload 콜백 — GameFramework · 키트 DLL 연쇄 교체
    // ======================================================================

    void ModuleHost::onBeforeGameplayDllReload()
    {
        onBeforeGameReload();
    }

    void ModuleHost::onAfterGameplayDllReload( void* /*hLibraryModule*/ )
    {
        if ( engine::areEngineServicesBound() == false )
            return;
        for ( const unique_ptr<Scene>& scene : engine::getSceneManager().getLoadedScenes() )
        {
            if ( scene != nullptr )
                scene->getObjectManager()->markTickStagesDirty(); // 기본값은 다시 찍지 않는다(ModuleTypeRegistry 의 같은 자리 참고)
        }
    }

    bool ModuleHost::onBeforeCommitBatch( const vector<string>& listModuleName )
    {
#if !defined( SW_SHIPPING )
        for ( const string& name : listModuleName )
        {
            if ( name != sw::config::kTargetEditorModule )
            {
                // 게임 상태는 새 이미지가 넘겨받을 유일한 것이다. 찍지 못했으면 내리지 않는다 — 옛 게임이 그대로 돈다.
                if ( suspendModulesInternal( ModuleScope::Game, true, true ) == false )
                {
                    SW_LOG_ERROR( "The game could not serialize its state for the reload — keeping the old game modules running" );
                    return false;
                }
                return true;
            }
        }
#else
        (void)listModuleName;
#endif
        return true;
    }

    bool ModuleHost::isEditorImageUsable( void* pLibraryModule ) const
    {
        EditorAPI api{};
        if ( ModuleHostInternal::exportApiFromImage<EditorAPI, PFN_ExportEditorAPI>( pLibraryModule, ModuleHostInternal::kEditorSymbols, api ) == false )
            return false;
        return api.create != nullptr && api.destroy != nullptr;
    }

    bool ModuleHost::isGameImageUsable( void* pLibraryModule ) const
    {
        GameAPI api{};
        if ( ModuleHostInternal::exportApiFromImage<GameAPI, PFN_ExportGameAPI>( pLibraryModule, ModuleHostInternal::kGameSymbols, api ) == false )
            return false;
        return api.create != nullptr && api.destroy != nullptr;
    }

    // ======================================================================
    // 보조 — 비우기 · 그래프 깨짐 표시
    // ======================================================================

    void ModuleHost::drainRenderWorkers()
    {
        if ( _pRenderThread != nullptr )
            _pRenderThread->waitIdle();
        // **디바이스가 없는 RHI 가 있다.** 백엔드 교체가 실패하면 RHI 객체는 남고 디바이스만 사라지는데, `getDevice()` 는 널
        // 참조를 반환하므로 그 상태로 물으면 죽는다. 종료 경로가 반드시 이곳을 지난다(`EngineLoop::shutdown` 도 같은 이유로
        // `hasDevice()` 를 먼저 확인한다).
        if ( _pRHI != nullptr && _pRHI->hasDevice() )
            _pRHI->getDevice().waitIdle();

        // 렌더 워커가 일을 끝내고 쉬게 됐으면 에디터의 "렌더 대기" 표시도 함께 버려야 한다. 그 표시는 렌더 스레드의
        // postPresent 만 풀 수 있는데, 그 스레드는 방금 일을 끝내고 쉬고 있다. 알려 주지 않으면 다음 updateUi 나 shutdown 이
        // waitForDrawSnapshotIdle 에서 영원히 돌아오지 않는다. 에디터 모듈 핫 리로드가 실제로 여기서 멈췄다.
        if ( _editor != nullptr && _editorApi.abandonPendingDraw != nullptr )
            _editorApi.abandonPendingDraw( _editor );

        // 모듈을 내리기 전에 비동기 태스크가 모두 끝나기를 기다린다. 제한 시간은 LiveReloadManager 의 폴백과 공유한다.
        if ( engine::areEngineServicesBound() )
        {
            if ( engine::getTaskManager().waitAll( LiveReloadManager::kModuleDrainTimeoutMs ) == false )
            {
                SW_LOG_ERROR( "Task fencing timeout (%# ms) before module reload — marking the LiveReload graph broken.", LiveReloadManager::kModuleDrainTimeoutMs );
                markReloadGraphBroken( "task fencing timeout before unload" );
            }
        }
    }

    void ModuleHost::onEditorReloadFault( uint32 faultCode )
    {
        // 반쯤 만든 인스턴스를 부수는 코드도 결함을 낸 그 모듈이다. 부르지 않고 잊는다(새는 것은 재시작이 치운다).
        SW_LOG_ERROR( "Editor module faulted after the reload (code 0x%#) — the editor is off until restart",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
        _editor    = nullptr;
        _editorApi = {};
    }

    void ModuleHost::onGameReloadFault( uint32 faultCode )
    {
        // 스냅숏은 **버리지 않는다.** 게임 컴포넌트는 리로드 앞에서 이미 모든 씬에서 걷어 냈다(`destroyGameInstance`) — 여기서 비우면 그
        // 상태로 저장할 때 컴포넌트가 빠진 씬이 저장된다. 고쳐서 다시 빌드하면 다음 리로드가 이 스냅숏을 되돌린다(게임이 없으니 새로 찍지 않는다).
        // 그때까지 씬 저장은 막혀 있다(`destroyGameInstance` 가 막았다).
        SW_LOG_ERROR( "Game module faulted after the reload (code 0x%#) — the game is off; fix it and rebuild, the next reload restores the %# byte snapshot (scene saving is blocked until then)",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ), static_cast<uint64>( _listGameSavedState.size() ) );
        _game    = nullptr;
        _gameApi = {};
    }

    void ModuleHost::markReloadGraphBroken( const utf8* pReason )
    {
#if !defined( SW_SHIPPING )
        if ( _pLiveReloadManager != nullptr )
            _pLiveReloadManager->markGraphBroken( pReason != nullptr ? pReason : "unspecified" );
#else
        (void)pReason;
#endif
    }

    // ======================================================================
    // API 바인딩
    // ======================================================================

    bool ModuleHost::bindEditorApi( void* pLibraryModule )
    {
        if ( ModuleHostInternal::exportApiFromImage<EditorAPI, PFN_ExportEditorAPI>( pLibraryModule, ModuleHostInternal::kEditorSymbols, _editorApi ) == false )
            return false;

        rebindEditorService();

        engine::registerModuleTypes( sw::config::kTargetEditorModule );
        return _editorApi.create != nullptr && _editorApi.destroy != nullptr;
    }

    bool ModuleHost::bindGameApi( void* pLibraryModule )
    {
        _gameApi = {};
#if defined( SW_SHIPPING )
        (void)pLibraryModule;
        if ( exportGameApi( &_gameApi ) == false )
        {
            SW_LOG_ERROR( "Failed to bind GameAPI (shipping)" );
            return false;
        }
#else
        // Shipping 은 게임을 정적으로 링크하므로(위 분기) 테이블이 어긋날 수 없다. 대조는 동적 경로에서만 한다.
        if ( ModuleHostInternal::exportApiFromImage<GameAPI, PFN_ExportGameAPI>( pLibraryModule, ModuleHostInternal::kGameSymbols, _gameApi ) == false )
            return false;
#endif

        rebindGameService();

        engine::registerModuleTypes( sw::config::kTargetGameModule );
        return _gameApi.create != nullptr && _gameApi.destroy != nullptr;
    }

    void ModuleHost::suspendModules( ModuleScope scope, bool bReleaseApiTable )
    {
        // 종료 · RHI 교체 · 에디터 리로드에는 게임 상태를 넘겨받을 새 게임 이미지가 없다 — 찍기 실패가 내리기를 막지 않는다.
        (void)suspendModulesInternal( scope, bReleaseApiTable, false );
    }

    bool ModuleHost::suspendModulesInternal( ModuleScope scope, bool bReleaseApiTable, bool bKeepGameOnCaptureFailure )
    {
        drainRenderWorkers();

        const bool bSuspendEditor = scope != ModuleScope::Game;
        const bool bSuspendGame   = scope != ModuleScope::Editor;

        // 무엇을 내리든 에디터 시뮬레이션부터 멈춘다. 게임만 내리면 에디터가 사라진 게임을 계속 돌리려 하고, 에디터를 내리면 —
        // 월드 플레이 상태는 에디터보다 오래 사는 `SceneManager` · 오브젝트 매니저에 있으므로 — 새로 만든 에디터는 멈춤으로 시작하는데
        // 월드는 계속 플레이 중이다(편집한 컴포넌트가 onBeginPlay 를 받고, 플레이 스냅샷은 복원되지 않는다). 멈춤이 플레이를 끝내고
        // 스냅샷을 되돌린다 — 에디터 컨텍스트가 아직 있는 동안이어야 한다.
        const bool bStopSimulation = hasEditor() && _editorApi.stopSimulation != nullptr;
        if ( bStopSimulation )
        {
            SW_LOG_INFO( "Stopping editor simulation before module suspend." );
            _editorApi.stopSimulation( _editor );
        }

        // 무엇이든 내리기 전에 찍는다. 찍지 못했으면(리로드 직전일 때만) 여기서 멈춘다 — 아직 아무것도 내리지 않았다.
        if ( bSuspendGame && captureGameState() == false && bKeepGameOnCaptureFailure )
            return false;
        if ( bSuspendEditor )
        {
            destroyEditorInstance( bReleaseApiTable );
            // 멈춤 창구가 없던 에디터라도 월드가 플레이 중으로 남지 않게 한다. 에디터 없이 다시 돌면 beginFrame 이 다시 켠다.
            if ( engine::areEngineServicesBound() && engine::getSceneManager().isWorldPlaying() )
                engine::getSceneManager().setWorldPlaying( false );
        }
        if ( bSuspendGame )
            destroyGameInstance( bReleaseApiTable );
        return true;
    }

#if !defined( SW_SHIPPING )
    void ModuleHost::attachEditorInstance( const EditorAPI& editorApi, EditorHandle editor )
    {
        _editorApi = editorApi;
        _editor    = editor;
    }

    void ModuleHost::attachGameInstance( const GameAPI& gameApi, GameHandle game )
    {
        _gameApi = gameApi;
        _game    = game;
    }
#endif

    bool ModuleHost::reinitializeAfterRhiSwap( void* pEditorModule, void* pGameModule )
    {
        if ( _pRHI == nullptr || _pRHI->hasDevice() == false )
            return false;

        // 기동과 같은 순서다 — 게임이 먼저 서고 에디터가 그 타입 · 서비스를 본다.
        bool bOk = true;
        if ( recreateGameInstance( pGameModule ) == false )
            bOk = false;
        if ( recreateEditorInstance( pEditorModule ) == false )
            bOk = false;
        return bOk;
    }

    void* ModuleHost::getLoadedModuleHandle( [[maybe_unused]] string_view moduleName ) const
    {
#if defined( SW_SHIPPING )
        // 정적 링크라 "로드된 모듈" 이라는 것이 없다. 부르는 쪽이 nullptr 을 처리한다.
        return nullptr;
#else
        if ( _pLiveReloadManager == nullptr )
            return nullptr;
        return _pLiveReloadManager->getModuleHandle( moduleName );
#endif
    }

    bool ModuleHost::captureGameState()
    {
        if ( _game == nullptr || _gameApi.serializeState == nullptr )
            return true;

        uint32 size{ 0 };
        if ( _gameApi.serializeState( _game, nullptr, &size ) == false )
            return false;
        if ( size == 0 )
            return true;

        vector<uint8> tempState( size );
        if ( _gameApi.serializeState( _game, tempState.data(), &size ) == false )
            return false;
        _listGameSavedState = std::move( tempState );
        return true;
    }

    void ModuleHost::restoreGameState()
    {
        if ( _game == nullptr )
            return;
        // 되돌릴 것이 없으면(상태 직렬화가 없는 게임 · 스냅숏이 비었다) 막을 이유도 없다.
        if ( _gameApi.deserializeState == nullptr || _listGameSavedState.empty() )
        {
            if ( engine::areEngineServicesBound() )
                engine::getSceneManager().setSaveBlockReason( {} );
            return;
        }

        if ( _gameApi.deserializeState( _game, _listGameSavedState.data(), static_cast<uint32>( _listGameSavedState.size() ) ) )
        {
            SW_LOG_INFO( "Scene object state restored from %zu bytes.", _listGameSavedState.size() );
            _listGameSavedState.clear();
            if ( engine::areEngineServicesBound() )
                engine::getSceneManager().setSaveBlockReason( {} );
        }
        else
        {
            SW_LOG_ERROR( "Failed to restore game state — keeping saved state for subsequent reload." );
        }
    }

    bool ModuleHost::recreateEditorInstance( void* pEditorModule )
    {
        if ( _bEnableEditor == SW_FALSE )
            return true;

        // 테이블이 비어 있으면 모듈에서 다시 바인딩해야 한다 — 리로드 경로가 그 일을 한다.
        if ( _editorApi.create == nullptr || _editorApi.initialize == nullptr )
        {
            onAfterEditorReload( pEditorModule );
            return _editor != nullptr;
        }

        rebindEditorService();
        return createEditorInstance();
    }

    bool ModuleHost::recreateGameInstance( void* pGameModule )
    {
        if ( _gameApi.create == nullptr || _gameApi.initialize == nullptr )
        {
#if defined( SW_SHIPPING )
            (void)pGameModule;
            onAfterGameReload( nullptr );
#else
            onAfterGameReload( pGameModule );
#endif
            return _game != nullptr;
        }

        rebindGameService();
        if ( createGameInstance() == false )
            return false;

        restoreGameState();
        return true;
    }

    // ======================================================================
    // 인스턴스 생성 · 파괴 — 리로드 경로와 RHI 핫스왑 경로가 같은 코드를 쓴다
    // ======================================================================

    void ModuleHost::rebindEditorService()
    {
        if ( _editorApi.bindService == nullptr )
            return;

        ModuleService editorService{};
        ModuleHostInternal::fillModuleService<ModuleHostInternal::Target::Editor>( this, editorService );
        _editorApi.bindService( &editorService );
    }

    void ModuleHost::rebindGameService()
    {
        if ( _gameApi.bindService == nullptr )
            return;

        ModuleService gameService{};
        ModuleHostInternal::fillModuleService<ModuleHostInternal::Target::Game>( this, gameService );
        _gameApi.bindService( &gameService );
    }

    void ModuleHost::destroyEditorInstance( bool bReleaseApiTable )
    {
        ModuleHostInternal::destroyInstance( _editorApi, _editor, bReleaseApiTable, sw::config::kTargetEditorModule );
    }

    void ModuleHost::destroyGameInstance( bool bReleaseApiTable )
    {
        ModuleHostInternal::destroyInstance( _gameApi, _game, bReleaseApiTable, sw::config::kTargetGameModule );
#if !defined( SW_SHIPPING )
        // 게임 컴포넌트를 모든 씬에서 걷어 냈다. 되돌릴 때까지(`restoreGameState`) 씬을 저장하면 그것들이 빠진 채 저장된다 — 리로드가 실패 ·
        // 중단되면 되돌리는 쪽이 오지 않으므로 여기서 막는다.
        if ( bReleaseApiTable && engine::areEngineServicesBound() )
            engine::getSceneManager().setSaveBlockReason( "the game module's components were removed for a reload and have not been restored yet" );
#endif
    }

    bool ModuleHost::createEditorInstance()
    {
        SW_MEMORY_SCOPE( Editor );
        return ModuleHostInternal::createInstance( _editorApi, _editor, _pWindow, _pRHI, true, "Editor" );
    }

    bool ModuleHost::createGameInstance()
    {
        SW_MEMORY_SCOPE( Game );
        return ModuleHostInternal::createInstance( _gameApi, _game, _pWindow, _pRHI, _bDedicatedServer == SW_FALSE, "Game" );
    }
} // namespace sw
