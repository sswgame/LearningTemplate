#include "pch.h"

#include "App/EditorModuleHost.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleImageUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Module/ModuleCatalog.h"
#include "Engine/Module/ModuleTypeRegistry.h"
#include "Engine/Object/Component/CameraComponent.h"

#include "ModuleHost/LiveReloadManager.h"
#include "ModuleHost/ModuleInstanceUtil.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    namespace
    {
        /** @brief 이 TU 전용 도우미 모음입니다(유니티 빌드에서 이름이 충돌하지 않도록 TU 이름을 붙입니다). */
        struct EditorModuleHostInternal
        {
            static constexpr ModuleAPISymbols kEditorSymbols{ "getEditorModuleAbiVersion", "getEditorModuleAbiStamp", "exportEditorAPI", "Editor" };

#if !defined( SW_SHIPPING )
            /**
             * @brief 에디터 인스턴스 없이 에디터 모듈만 올려 진입점 @p pSymbol 하나를 부르고 내립니다(헤드리스 임포트 · 로컬라이제이션 공통).
             * @details 모듈 ABI 를 대조하고, 모듈 타입을 등록했다가 걷습니다. @p call 은 `( TEntryFn, const ModuleService& ) -> bool` 이고 엔진 서비스 표를 받습니다.
             * @param pPurpose 로그에 쓰는 작업 이름입니다(`"Asset importing"`).
             */
            template <typename TEntryFn, typename TCall>
            [[nodiscard]] static bool callEditorModuleEntry( const utf8* pSymbol, const utf8* pPurpose, TCall&& call )
            {
                const string modulePath     = ModuleImageUtil::findModuleLibraryPath( sw::config::kTargetEditorModule );
                void* const  pLibraryModule = FileUtil::exists( modulePath ) ? ModuleImageUtil::loadDynamicLibrary( modulePath ) : nullptr;
                if ( pLibraryModule == nullptr )
                {
                    SW_LOG_ERROR( "%# needs the editor module in Bin/Modules: %#", pPurpose, modulePath.c_str() );
                    return false;
                }
                (void)ModuleImageUtil::bindDelayLoadImports( pLibraryModule ); // 못 묶으면 경고했다

                // 올리는 순간 모듈의 정적 등록기가 전역 머리에 매달린다. 모듈 이름으로 등록해 두어야 내리기 전에 걷을 수 있다.
                engine::registerModuleTypes( sw::config::kTargetEditorModule );

                bool bSucceeded = false;
                if ( ModuleInstanceUtil::matchesModuleAbi( pLibraryModule, kEditorSymbols ) )
                {
                    const TEntryFn pfnEntry = reinterpret_cast<TEntryFn>( ModuleImageUtil::getDynamicSymbol( pLibraryModule, pSymbol ) );
                    if ( pfnEntry == nullptr )
                        SW_LOG_ERROR( "The editor module does not export %#", pSymbol );
                    else
                    {
                        ModuleService service{};
                        engine::fillModuleServices( service );
                        bSucceeded = call( pfnEntry, static_cast<const ModuleService&>( service ) );
                    }
                }

                engine::unregisterModuleTypes( sw::config::kTargetEditorModule );
                // 내리지 못하면(다른 코드가 아직 그 이미지의 이벤트 채널을 구독한다) 프로세스 끝까지 올라와 있을 뿐이다 — 이유는 경고로 남는다.
                (void)ModuleImageUtil::unloadModuleImage( sw::config::kTargetEditorModule, pLibraryModule );
                return bSucceeded;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EditorModuleHost" );

    EditorModuleHost::EditorModuleHost()
        : ModuleHost{}
        , _editorAPI{}
        , _editor{ nullptr }
        , _bEnableEditor{ SW_FALSE }
        , _bEditorModuleActive{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    EditorModuleHost::~EditorModuleHost()
    {
        // 공통 호스트의 소멸자 안에서는 가상 함수가 이 클래스로 오지 않는다 — 에디터를 여기서 먼저 내린다.
        shutdown();
    }

    bool EditorModuleHost::loadModuleImages( LiveReloadManager* pLiveReloadManager, const ModuleCatalog& catalog, const ModuleResolution& resolution )
    {
        _bEditorModuleActive = resolution.isActive( config::kTargetEditorModule ) ? SW_TRUE : SW_FALSE;
        return ModuleHost::loadModuleImages( pLiveReloadManager, catalog, resolution );
    }

    bool EditorModuleHost::initialize( LiveReloadManager* pLiveReloadManager, RHI* pRHI, IWindow* pWindow, RenderThread* pRenderThread, bool bEnableEditor )
    {
        _bEnableEditor = bEnableEditor ? SW_TRUE : SW_FALSE;

        // 게임이 먼저, 에디터가 나중이다. 게임은 처음 여는 씬을 요청하고(`GameInstanceBase::requestFirstScene`) 에디터는 제 시작 씬
        // (`-gv_editorStartupScene`)을 요청한다. 씬 매니저는 마지막 요청을 남기므로 나중에 요청한 에디터의 씬이 열린다(`GameSettings::_startMap` 주석).
        if ( initializeGame( pLiveReloadManager, pRHI, pWindow, pRenderThread ) == false )
            return false;

#if !defined( SW_SHIPPING )
        if ( _bEnableEditor == SW_TRUE && _bEditorModuleActive == SW_FALSE )
        {
            // -EnableEditor 를 줬는데 매니페스트가 에디터 모듈을 껐다(프로젝트 · 구성) — 에디터 없이 조용히 뜨지 않는다.
            SW_LOG_ERROR( "-EnableEditor was given, but the module manifests turn %# off for this project", config::kTargetEditorModule );
            return false;
        }

        if ( _bEnableEditor == SW_TRUE && pLiveReloadManager != nullptr )
        {
            BLOCK( "에디터: 뷰포트 / EditorModule 등록" )
            {
                SW_MEMORY_SCOPE( Editor );
                pLiveReloadManager->setOnBeforeReload( config::kTargetEditorModule,
                                                       SW_DELEGATE_METHOD( LiveReloadManager::OnBeforeReloadDelegate, &EditorModuleHost::onBeforeEditorReload, this ) );
                pLiveReloadManager->setOnAfterReload( config::kTargetEditorModule,
                                                      SW_DELEGATE_METHOD( LiveReloadManager::OnAfterReloadDelegate, &EditorModuleHost::onAfterEditorReload, this ) );
                pLiveReloadManager->setOnReloadFault( config::kTargetEditorModule,
                                                      SW_DELEGATE_METHOD( LiveReloadManager::OnReloadFaultDelegate, &EditorModuleHost::onEditorReloadFault, this ) );
                pLiveReloadManager->setOnValidateImage( config::kTargetEditorModule,
                                                        SW_DELEGATE_METHOD( LiveReloadManager::OnValidateImageDelegate, &EditorModuleHost::isEditorImageUsable, this ) );
                if ( pLiveReloadManager->registerModule( config::kTargetEditorModule ) == false )
                {
                    SW_LOG_ERROR( "Editor Module 로드에 실패했습니다." );
                    return false;
                }

                if ( pLiveReloadManager->isGraphBroken() )
                {
                    SW_LOG_ERROR( "LiveReload graph broken during module registration — aborting initialize" );
                    return false;
                }
            }
        }
#endif

        return true;
    }

    bool EditorModuleHost::importAssetsWithEditorModule( EditorImportKind kind, bool bCheckOnly )
    {
#if defined( SW_SHIPPING )
        (void)kind;
        (void)bCheckOnly;
        SW_LOG_ERROR( "Asset importing needs the editor module, which a Shipping build does not have - run it from a Dev build." );
        return false;
#else
        // 인스턴스 없이 부르므로 엔진 서비스 표를 직접 넘긴다 — 임포터가 작업 시스템으로 압축을 나눈다.
        return EditorModuleHostInternal::callEditorModuleEntry<PFN_ImportEditorAssets>( kImportEditorAssetsSymbol, "Asset importing",
                                                                                        [kind, bCheckOnly]( PFN_ImportEditorAssets pfnImport, const ModuleService& service )
        {
            return pfnImport( static_cast<uint32>( kind ), bCheckOnly ? 1u : 0u, &service ) == 0;
        } );
#endif
    }

    bool EditorModuleHost::runLocalizationWithEditorModule( EditorLocalizationTask task, string_view poPath, string_view projectArgument )
    {
#if defined( SW_SHIPPING )
        (void)task;
        (void)poPath;
        (void)projectArgument;
        SW_LOG_ERROR( "Localization tools need the editor module, which a Shipping build does not have - run them from a Dev build." );
        return false;
#else
        const string poPathText( poPath );
        const string projectText( projectArgument );
        return EditorModuleHostInternal::callEditorModuleEntry<PFN_RunEditorLocalizationTask>(
            kRunEditorLocalizationTaskSymbol, "Localization tools", [task, &poPathText, &projectText]( PFN_RunEditorLocalizationTask pfnRun, const ModuleService& service )
        {
            return pfnRun( static_cast<uint32>( task ), poPathText.c_str(), projectText.c_str(), &service ) == 0;
        } );
#endif
    }

    // ======================================================================
    // 프레임 단위 처리
    // ======================================================================

    bool EditorModuleHost::queryGameplayActive() const
    {
        if ( hasEditor() == false || _editorAPI.isPlaying == nullptr )
            return true;
        return _editorAPI.isPlaying( _editor );
    }

    bool EditorModuleHost::queryTickScene() const
    {
        if ( hasEditor() == false )
            return true;
        if ( _editorAPI.isPaused != nullptr && _editorAPI.isPaused( _editor ) )
        {
            if ( _editorAPI.isPlaying == nullptr || _editorAPI.isPlaying( _editor ) == false )
                return false;
        }
        return true;
    }

    void EditorModuleHost::sampleViewTargets()
    {
        ModuleFrameState& frameState = getMutableFrameState();
        frameState._views            = HostViewTargets{};
        if ( hasEditor() == false )
            return;

        HostViewTarget& game = frameState._views._game;
        if ( _editorAPI.getGameViewport != nullptr )
            _editorAPI.getGameViewport( _editor, &game._renderTarget, &game._width, &game._height );
        HostViewTarget& scene = frameState._views._scene;
        if ( _editorAPI.getSceneViewport != nullptr )
            _editorAPI.getSceneViewport( _editor, &scene._renderTarget, &scene._width, &scene._height );
    }

    void EditorModuleHost::updateEditorUI( float32 /*deltaTime*/ )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false )
            return;

        if ( _editorAPI.updateUI != nullptr )
            _editorAPI.updateUI( _editor );

        // 에디터가 이번 프레임 입력을 처리한 **뒤에** 확정한다. Step 버튼은 이 갱신에서 눌리고, 씬을 한 칸 틱한 다음
        // endEditorFrame 에서 소비된다. 이 질의를 프레임 앞으로 옮기면 Step 이 틱 없이 소비되어 아무 일도 일어나지 않는다.
        getMutableFrameState()._bTickScene = queryTickScene() ? SW_TRUE : SW_FALSE;
        sampleViewTargets();
    }

    void EditorModuleHost::endEditorFrame()
    {
        SW_MEMORY_SCOPE( Editor );
        if ( hasEditor() == false || _editorAPI.endFrame == nullptr )
            return;
        _editorAPI.endFrame( _editor );
    }

    bool EditorModuleHost::onWindowMessage( const NativeWindowEvent& event )
    {
        if ( hasEditor() == false || _editorAPI.processEvent == nullptr )
            return false;

        // 에디터 내부 상태 갱신과 입력 필터링은 에디터 모듈 안에서 처리한다
        return _editorAPI.processEvent( _editor, &event );
    }

    CameraComponent* EditorModuleHost::getSceneViewCamera() const
    {
        if ( hasEditor() == false || _editorAPI.getSceneViewCamera == nullptr )
            return nullptr;
        return static_cast<CameraComponent*>( _editorAPI.getSceneViewCamera( _editor ) );
    }

    // ======================================================================
    // LiveReload 콜백 — 에디터
    // ======================================================================

    void EditorModuleHost::onBeforeEditorReload()
    {
        suspendModules( ModuleScope::Editor, true );
    }

    void EditorModuleHost::onAfterEditorReload( void* pLibraryModule )
    {
        SW_MEMORY_SCOPE( Editor );
        if ( bindEditorAPI( pLibraryModule ) == false )
        {
            markReloadGraphBroken( "EditorAPI bind failed after reload" );
            return;
        }

        if ( createEditorInstance() == false )
        {
            _editorAPI = {};
            markReloadGraphBroken( "Editor create/initialize failed after reload" );
            return;
        }
    }

    bool EditorModuleHost::isEditorImageUsable( void* pLibraryModule ) const
    {
        EditorAPI api{};
        if ( ModuleInstanceUtil::exportAPIFromImage<EditorAPI, PFN_ExportEditorAPI>( pLibraryModule, EditorModuleHostInternal::kEditorSymbols, api ) == false )
            return false;
        return api.create != nullptr && api.destroy != nullptr;
    }

    void EditorModuleHost::onEditorReloadFault( uint32 faultCode )
    {
        // 반쯤 만든 인스턴스를 부수는 코드도 결함을 낸 그 모듈이다. 부르지 않고 잊는다(새는 것은 재시작이 치운다).
        SW_LOG_ERROR( "Editor module faulted after the reload (code 0x%#) — the editor is off until restart",
                      Fmt( faultCode, Format( 8, Format::Padding::Zero ).hex() ) );
        _editor    = nullptr;
        _editorAPI = {};
    }

    // ======================================================================
    // 공통 호스트가 부르는 자리 — 내리기 · 비우기
    // ======================================================================

    void EditorModuleHost::onBeforeSuspendModules()
    {
        const bool bStopSimulation = hasEditor() && _editorAPI.stopSimulation != nullptr;
        if ( bStopSimulation )
        {
            SW_LOG_INFO( "Stopping editor simulation before module suspend." );
            _editorAPI.stopSimulation( _editor );
        }
    }

    void EditorModuleHost::suspendHostModule( bool bReleaseAPITable )
    {
        destroyEditorInstance( bReleaseAPITable );
    }

    void EditorModuleHost::onRenderWorkersDrained()
    {
        if ( _editor != nullptr && _editorAPI.abandonPendingDraw != nullptr )
            _editorAPI.abandonPendingDraw( _editor );
    }

    // ======================================================================
    // API 바인딩 · 인스턴스
    // ======================================================================

    bool EditorModuleHost::bindEditorAPI( void* pLibraryModule )
    {
        if ( ModuleInstanceUtil::exportAPIFromImage<EditorAPI, PFN_ExportEditorAPI>( pLibraryModule, EditorModuleHostInternal::kEditorSymbols, _editorAPI ) == false )
            return false;

        rebindEditorService();

        engine::registerModuleTypes( sw::config::kTargetEditorModule );
        return _editorAPI.create != nullptr && _editorAPI.destroy != nullptr;
    }

#if !defined( SW_SHIPPING )
    void EditorModuleHost::attachEditorInstance( const EditorAPI& editorAPI, EditorHandle editor )
    {
        _editorAPI = editorAPI;
        _editor    = editor;
    }
#endif

    bool EditorModuleHost::reinitializeAfterRHISwap( void* pEditorModule, void* pGameModule )
    {
        RHI* const pRHI = getRHI();
        if ( pRHI == nullptr || pRHI->hasDevice() == false )
            return false;

        // 기동과 같은 순서다 — 게임이 먼저 서고 에디터가 그 타입 · 서비스를 본다.
        bool bOk = true;
        if ( recreateGameInstance( pGameModule ) == false )
            bOk = false;
        if ( recreateEditorInstance( pEditorModule ) == false )
            bOk = false;
        return bOk;
    }

    bool EditorModuleHost::recreateEditorInstance( void* pEditorModule )
    {
        if ( _bEnableEditor == SW_FALSE )
            return true;

        // 테이블이 비어 있으면 모듈에서 다시 바인딩해야 한다 — 리로드 경로가 그 일을 한다.
        if ( _editorAPI.create == nullptr || _editorAPI.initialize == nullptr )
        {
            onAfterEditorReload( pEditorModule );
            return _editor != nullptr;
        }

        rebindEditorService();
        return createEditorInstance();
    }

    void EditorModuleHost::rebindEditorService()
    {
        if ( _editorAPI.bindService == nullptr )
            return;

        ModuleService editorService{};
        fillModuleService( editorService, false );
        _editorAPI.bindService( &editorService );
    }

    void EditorModuleHost::destroyEditorInstance( bool bReleaseAPITable )
    {
        ModuleInstanceUtil::destroyInstance( _editorAPI, _editor, bReleaseAPITable, sw::config::kTargetEditorModule );
    }

    bool EditorModuleHost::createEditorInstance()
    {
        SW_MEMORY_SCOPE( Editor );
        return ModuleInstanceUtil::createInstance( _editorAPI, _editor, getWindow(), getRHI(), true, "Editor" );
    }
} // namespace sw
