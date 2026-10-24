#include "pch.h"

#include "Editor/ImGuiEditor.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Backend/IImGuiPlatformBackend.h"
#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/ImGuiOsInputFilter.h"
#include "Editor/Common/Commands/Play/EditorPlayCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorFontSetup.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/EditorPanelDump.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPlayToolbar.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/GUI/EditorUIContext.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorExtensionRegistryDump.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Common/EngineSubsystemHost.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/EditSessionState.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "RuntimeAPI/Export/EditorModuleExports.h"

#include <imgui.h>
#include <ImGuiNotify.hpp>
#include <ImGuizmo.h>
#include <implot.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiEditorInternal
        {
            /**
             * @brief 등록된 프레임 단계를 `_order` 순서로 모두 부릅니다.
             * @details 개수와 항목을 매번 다시 읽는다. 단계가 모듈을 로드하거나 언로드해 목록이 바뀌어도 지난 배열을 읽지 않는다.
             */
            static void runFrameSteps( EditorContext& context )
            {
                using Registry = EditorExtensionRegistry<EditorFrameStepRegistration>;
                for ( uint32 index = 0; index < Registry::getCount(); ++index )
                {
                    const EditorFrameStepRegistration& registration = Registry::getAt( index );
                    if ( registration._pfnRun != nullptr )
                        registration._pfnRun( context );
                }
            }

            static constexpr Color kNotificationWindowBg{ 0.10f, 0.10f, 0.10f, 1.00f };

            /// @brief 글자 배율(모니터 DPI)과 테마 배율이 이 값보다 크게 다르면 테마 배율을 글자 배율에 맞춘다(beginFrame).
            static constexpr float32 kDpiFollowTolerance = 0.001f;

            /// @brief ImGui 는 io.DeltaTime 이 0 이하이면 단언한다. 엔진 경과가 0 인 프레임(일시 정지 직후)에도 이 값 아래로 내리지 않는다.
            static constexpr float32 kMinDeltaTime = 0.00001f;

            /** @brief 무인 자동화가 사람의 장치 입력을 막는 중이면 true 입니다. 이때 플랫폼 백엔드가 ImGui 에 넣는 실제 커서와 키를 지웁니다. */
            static bool isOsDeviceInputBlocked()
            {
                const InputManager* pInput = editor::findSubsystem<InputManager>();
                return pInput != nullptr && pInput->isOsDeviceInputBlocked();
            }

            /** @brief 뷰의 렌더 타깃 핸들 · 크기 · 창 안 자리를 호스트 형식으로 씁니다. @p pView 가 없으면 빈 값이며, 이번 프레임에 그 뷰를 그리지 않았다는 뜻입니다. */
            static void writeViewport( const EditorViewTarget* pView, HostViewTarget* pOutTarget )
            {
                if ( pOutTarget == nullptr )
                    return;
                *pOutTarget = HostViewTarget{};
                if ( pView == nullptr )
                    return;
                pOutTarget->_renderTarget = pView->_renderTarget;
                pOutTarget->_width        = pView->_width;
                pOutTarget->_height       = pView->_height;
                pOutTarget->_windowX      = pView->_windowX;
                pOutTarget->_windowY      = pView->_windowY;
            }

            /** @brief ImGui 할당을 sw 할당자로 보냅니다. 할당 헤더에 태그가 기록되어 메모리 프로파일러의 Editor 항목으로 집계됩니다. */
            static void* allocateForImGui( size_t size, void* /*pUserData*/ ) { return Memory::allocate( size ); }
            /** @brief `allocateForImGui` 로 할당한 메모리를 해제합니다. */
            static void freeForImGui( void* pPtr, void* /*pUserData*/ ) { Memory::free( pPtr ); }

            /**
             * @brief 스플래시 로드 작업 둘의 결과입니다. 작업마다 제 칸만 쓰고, 기동 스레드는 단계(`waitStage`)가 끝난 뒤에 읽는다.
             * @details 두 파일은 엔진 필수 데이터다. 읽지 못해도 경고만 남기고 기동하면 렌더 경로가 틀린 채로 돈다 — 결과를 기동 단계가 받아 멈춘다.
             */
            struct SplashLoadResult
            {
                bool _bRenderPassLoaded{ false };
                bool _bForwardPipelineLoaded{ false };
            };

            static void loadSplashDefaultRenderPass( const TaskArgs& args )
            {
                shared_ptr<RenderPassAsset>  pPass   = args.get<shared_ptr<RenderPassAsset>>( 0 );
                shared_ptr<SplashLoadResult> pResult = args.get<shared_ptr<SplashLoadResult>>( 1 );
                // `findSubsystem<T>()` 는 nullptr 을 반환할 수 있다. 이 둘은 워커 스레드에서 도는 스플래시 로드라, 서비스 연결이 아직
                // 없거나 이미 끊긴 시점에 걸리면 조용히 크래시한다. 그때는 결과가 false 로 남아 기동이 멈춘다.
                const EngineDefaultAssets* pEngineDefaultAssets = editor::findSubsystem<const EngineDefaultAssets>();
                const bool                 bNotReady            = pPass == nullptr || pResult == nullptr || pEngineDefaultAssets == nullptr;
                if ( bNotReady )
                    return;
                SW_LOG_TRACE( "Splash: reading DefaultRenderPass.xml" );
                pResult->_bRenderPassLoaded = pPass->loadFromXMLFile( pEngineDefaultAssets->_defaultRenderPass.get() );
                if ( pResult->_bRenderPassLoaded == false )
                    SW_LOG_ERROR( "Splash: could not read %#", pEngineDefaultAssets->_defaultRenderPass.c_str() );
            }

            static void loadSplashForwardPipeline( const TaskArgs& args )
            {
                shared_ptr<RenderPipelineAsset> pPipeline            = args.get<shared_ptr<RenderPipelineAsset>>( 0 );
                shared_ptr<SplashLoadResult>    pResult              = args.get<shared_ptr<SplashLoadResult>>( 1 );
                const EngineDefaultAssets*      pEngineDefaultAssets = editor::findSubsystem<const EngineDefaultAssets>();
                const bool                      bNotReady            = pPipeline == nullptr || pResult == nullptr || pEngineDefaultAssets == nullptr;
                if ( bNotReady )
                    return;
                SW_LOG_TRACE( "Splash: reading ForwardPipeline.xml" );
                pResult->_bForwardPipelineLoaded = pPipeline->loadFromXMLFile( pEngineDefaultAssets->_defaultForwardPipeline.get() );
                if ( pResult->_bForwardPipelineLoaded == false )
                    SW_LOG_ERROR( "Splash: could not read %#", pEngineDefaultAssets->_defaultForwardPipeline.c_str() );
            }

            /** @brief 인스턴스가 종료하기 전에 선택 · 미저장 표시를 엔진(`EditSessionState`)에 맡깁니다. 다음 인스턴스가 `resumeEditSession` 으로 찾아갑니다. */
            static void storeEditSession( EditorContext& context )
            {
                EditSessionState* pSession      = editor::findSubsystem<EditSessionState>();
                SceneManager*     pSceneManager = editor::findSubsystem<SceneManager>();
                if ( pSession == nullptr || pSceneManager == nullptr )
                    return;
                const EditorWorkspace&     workspace = context.getSubsystem<EditorWorkspace>();
                const EditorSelection&     selection = editor::getMainSceneSelection();
                EditSessionState::Snapshot snapshot{};
                // 워크스페이스가 아직 이 세대를 보지 못했으면 선택 · 표시는 옛 씬의 것이다 — 세대를 그대로 적어 다음 인스턴스가 버리게 한다.
                snapshot._sceneGeneration = workspace.getObservedSceneGeneration();
                snapshot._bSceneDirty     = workspace.isSceneDirty();
                for ( const GameObjectHandle& handle : selection.getSelectedHandles() )
                {
                    snapshot._listSelectedObjectID.push_back( handle.objectID() );
                }
                snapshot._listSelectedAsset = selection.getSelectedAssets();
                pSession->store( std::move( snapshot ) );
            }

            /**
             * @brief 앞선 인스턴스가 맡긴 선택 · 미저장 표시를 되찾습니다. 앞선 인스턴스가 있었으면 true 입니다(씬이 그 뒤에 바뀌었으면 값은 버리지만 true).
             * @details 워크스페이스는 지금 씬 세대를 이미 본 것으로 둔다 — 새 인스턴스가 지금 씬을 "새 씬" 으로 여겨 엔진의 되돌리기 스택을 비우지 않게 한다
             *          (`EditorSceneGenerationSync`). 씬 오브젝트는 같은 id 로 이어지므로(게임 상태 복원 · 같은 씬) 선택 id 와 되돌리기 명령이 그대로 맞는다.
             */
            static bool resumeEditSession( EditorContext& context )
            {
                EditSessionState* pSession      = editor::findSubsystem<EditSessionState>();
                SceneManager*     pSceneManager = editor::findSubsystem<SceneManager>();
                if ( pSession == nullptr || pSceneManager == nullptr )
                    return false;
                const bool       bResumed   = pSession->hasSnapshot();
                const uint64     generation = pSceneManager->getSceneGeneration();
                EditorWorkspace& workspace  = context.getSubsystem<EditorWorkspace>();
                workspace.setObservedSceneGeneration( generation );
                EditSessionState::Snapshot snapshot{};
                if ( pSession->take( generation, snapshot ) == false )
                    return bResumed;
                if ( snapshot._bSceneDirty )
                    workspace.markSceneDirty();
                vector<GameObject*> listObject;
                for ( const uint64 objectID : snapshot._listSelectedObjectID )
                {
                    if ( GameObject* pObject = editor::findGameObject( GameObjectHandle::make( objectID ) ) )
                        listObject.push_back( pObject );
                }
                EditorSelection& selection = editor::getMainSceneSelection();
                if ( listObject.empty() == false )
                    selection.selectObjects( listObject, SelectionMode::Replace );
                if ( snapshot._listSelectedAsset.empty() == false )
                    selection.selectAssets( snapshot._listSelectedAsset, SelectionMode::Replace );
                SW_LOG_INFO( "Resumed the edit session: %# selected object(s), %#", static_cast<uint32>( listObject.size() ),
                             snapshot._bSceneDirty ? "unsaved changes" : "no unsaved changes" );
                return true;
            }

            /**
             * @brief 이 모듈이 Undo 스택에 넣은 모듈 명령(패널 람다, 문서 편집)과 알림 처리기를 제거합니다. 오브젝트 편집(엔진 데이터 명령)은 남깁니다.
             * @details Undo 스택은 엔진이 소유하므로 이 모듈보다 오래 산다. 모듈 명령을 남긴 채 모듈을 언로드하면 리로드 뒤 Ctrl+Z 가 언맵된 코드로 점프하고,
             *          종료할 때는 델리게이트 소멸자(`Delegate::_managerFunc`)가 그 코드로 점프한다. 제거 범위는 이 함수가 속한 이미지다.
             */
            static void releaseModuleUndoCommands()
            {
                CommandStack* pCommandStack = editor::findSubsystem<CommandStack>();
                const void*   pBegin{ nullptr };
                const void*   pEnd{ nullptr };
                if ( pCommandStack == nullptr ||
                     ModuleImageUtil::findLoadedImageRange( reinterpret_cast<const void*>( &ImGuiEditorInternal::releaseModuleUndoCommands ), pBegin, pEnd ) == false )
                    return;
                pCommandStack->setObjectEditListener( {} );
                const uint32 droppedCount = pCommandStack->removeCodeWithin( pBegin, pEnd );
                if ( droppedCount > 0 )
                    SW_LOG_INFO( "Dropped %# undo commands that run editor module code; scene edits stay undoable", droppedCount );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 에디터 기동 단계 `EditorData` 입니다(`ImGuiEditor::initializeEditorData`). */
    class ImGuiEditor::EditorDataSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorData";

        explicit EditorDataSubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult initializeSubsystem() override { return _editor.initializeEditorData(); }
        void                shutdownSubsystem() override { _editor.shutdownEditorData(); }
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask   getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };

    /** @brief 에디터 기동 단계 `ImGuiContext` 입니다(`ImGuiEditor::initializeImGuiContext`). */
    class ImGuiEditor::ImGuiContextSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ImGuiContext";

        explicit ImGuiContextSubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult     initializeSubsystem() override { return _editor.initializeImGuiContext(); }
        void                    shutdownSubsystem() override { _editor.shutdownImGuiContext(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { EditorDataSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };

    /** @brief 에디터 기동 단계 `PlatformBackend` 입니다(`ImGuiEditor::initializePlatformBackend`). */
    class ImGuiEditor::PlatformBackendSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "PlatformBackend";

        explicit PlatformBackendSubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult     initializeSubsystem() override { return _editor.initializePlatformBackend(); }
        void                    shutdownSubsystem() override { _editor.shutdownPlatformBackend(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { ImGuiContextSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };

    /** @brief 에디터 기동 단계 `RendererBackend` 입니다(`ImGuiEditor::initializeRendererBackend`). */
    class ImGuiEditor::RendererBackendSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "RendererBackend";

        explicit RendererBackendSubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult     initializeSubsystem() override { return _editor.initializeRendererBackend(); }
        void                    shutdownSubsystem() override { _editor.shutdownRendererBackend(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { ImGuiContextSubsystem::kSubsystemName, PlatformBackendSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };

    /** @brief 에디터 기동 단계 `Splash` 입니다(`ImGuiEditor::loadSplashAssets`). */
    class ImGuiEditor::SplashSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "Splash";

        explicit SplashSubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult initializeSubsystem() override { return _editor.loadSplashAssets(); }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask   getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };

    /** @brief 에디터 기동 단계 `EditorContext` 입니다(`ImGuiEditor::initializeEditorContext`). */
    class ImGuiEditor::EditorContextSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorContext";

        explicit EditorContextSubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult     initializeSubsystem() override { return _editor.initializeEditorContext(); }
        void                    shutdownSubsystem() override { _editor.shutdownEditorContext(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { EditorDataSubsystem::kSubsystemName, ImGuiContextSubsystem::kSubsystemName, PlatformBackendSubsystem::kSubsystemName, RendererBackendSubsystem::kSubsystemName, SplashSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };

    /** @brief 에디터 기동 단계 `WindowCloseQuery` 입니다(`ImGuiEditor::initializeWindowCloseQuery`). */
    class ImGuiEditor::WindowCloseQuerySubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "WindowCloseQuery";

        explicit WindowCloseQuerySubsystem( ImGuiEditor& editor )
            : _editor{ editor }
        {
        }

        SubsystemInitResult     initializeSubsystem() override { return _editor.initializeWindowCloseQuery(); }
        void                    shutdownSubsystem() override { _editor.shutdownWindowCloseQuery(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { EditorContextSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        MemoryTag         getSubsystemMemoryTag() const override { return MemoryTag::Editor; }

    private:
        ImGuiEditor& _editor;
    };
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiEditor" );

    // 이 파일만 읽으므로 여기서 정의하고 헤더에는 선언하지 않는다.
    /**
     * @brief `-gv_editorStartupScene=<리소스 경로>`: 에디터가 시작할 때 이 씬을 엽니다.
     * @details 빈 씬으로 기동 검증을 하면 오브젝트를 순회하는 코드가 실행되지 않습니다. 뷰포트 피킹, 컴포넌트 시각화, Hierarchy 트리,
     *          Profiler 분포 테이블, 씬 세대 변경 훅이 그 예입니다. 이 스위치로 테스트 씬을 열면 이 경로가 모두 실행됩니다.
     *          예: `-gv_editorStartupScene=game/empty/maps/editortest.scene.xml`
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_editorStartupScene, "", "에디터 시작 시 열 씬의 리소스 경로 (비우면 열지 않는다)" );

    /**
     * @brief `-gv_editorUiScale=<배율>`: 에디터 UI 배율입니다. 0 이면 창이 놓인 모니터의 DPI 를 따릅니다.
     * @details 언리얼의 Editor Preferences UI 배율, 유니티의 UI Scaling 설정에 해당합니다. 모니터 DPI 와 상관없이 UI 를 크게 또는 작게 보고
     *          싶을 때, 그리고 고해상도 모니터가 없는 기계에서 배율 경로를 확인할 때 씁니다.
     */
    SW_GLOBAL_VARIABLE( float32, gv_editorUiScale, 0.0f, "에디터 UI 배율 (0 = 모니터 DPI 를 따름)" );

    ImGuiEditor::ImGuiEditor()
        : _startup{}
        , _pWindow{ nullptr }
        , _pRHIDevice{ nullptr }
        , _platformBackend{ nullptr }
        , _rendererBackend{ nullptr }
        , _editorToolDefaults{ nullptr }
        , _editorContext{ nullptr }
        , _dockLayout{}
        , _arrDrawSnapshot{}
        , _drawSnapshotHandoff{}
        , _lastDrawSnapshotSequence{ 0 }
        , _pUICapture{ nullptr }
        , _bInitialized{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    ImGuiEditor::~ImGuiEditor()
    {
        ImGuiEditor::shutdown();
    }

    bool ImGuiEditor::initialize( IWindow* pWindow, IRHIDevice* pRHIDevice )
    {
        SW_LOG_TRACE( "Initialize start." );
        if ( _bInitialized != SW_FALSE )
            return true;

        if ( pWindow == nullptr || pRHIDevice == nullptr )
        {
            SW_LOG_ERROR( "Cannot initialize without window and RHI device." );
            return false;
        }
        _pWindow    = pWindow;
        _pRHIDevice = pRHIDevice;

        // 단계마다 먼저 서야 하는 단계를 적는다. 목록이 그 순서로 초기화하고, 실패하면 초기화한 단계만 역순으로 종료한다.
        _startup.add( make_unique<EditorDataSubsystem>( *this ) );
        _startup.add( make_unique<ImGuiContextSubsystem>( *this ) );
        _startup.add( make_unique<PlatformBackendSubsystem>( *this ) );
        _startup.add( make_unique<RendererBackendSubsystem>( *this ) );
        _startup.add( make_unique<SplashSubsystem>( *this ) );
        _startup.add( make_unique<EditorContextSubsystem>( *this ) );
        _startup.add( make_unique<WindowCloseQuerySubsystem>( *this ) );
        if ( _startup.initializeAll( EngineSubsystemHost::kClient ) == false )
        {
            SW_LOG_ERROR( "Editor startup failed: %#", _startup.getError().c_str() );
            _startup.destroyAll();
            return false;
        }

        _bInitialized = SW_TRUE;
        return true;
    }

    SubsystemInitResult ImGuiEditor::initializeEditorData()
    {
        // 사용자 환경설정은 사용자 파일이다 — 처음 실행이면 없고, 깨졌으면 옆으로 치우고 새로 쓴다(`EditorPreferencesStore::loadAll`).
        if ( EditorPreferencesStore::loadAll( EditorPreferencesStore::getDefaultFilePath() ) == EditorUserFileResult::Missing )
            SW_LOG_INFO( "Editor preferences: first run - nothing changed from the defaults yet" );
        // 도구 시드는 에디터 필수 데이터다. 대신 쓸 값이 없으므로 읽지 못하면 기동을 멈춘다(이유는 로드가 키 이름과 남겼다).
        _editorToolDefaults = make_unique<EditorToolDefaults>();
        if ( _editorToolDefaults->loadFromHostPath() == false )
            return SubsystemInitResult::Failed;
        editor::setEditorToolDefaults( _editorToolDefaults.get() );
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownEditorData()
    {
        editor::setEditorToolDefaults( nullptr );
        _editorToolDefaults.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeImGuiContext()
    {
        SW_LOG_TRACE( "Checking ImGui version and creating context" );
        IMGUI_CHECKVERSION();
        // 컨텍스트보다 먼저 등록한다. 그래야 이 모듈의 ImGui 할당과 해제가 모두 같은 할당자를 거친다. 컨텍스트는 shutdown 이 이 모듈 안에서 지운다.
        ImGui::SetAllocatorFunctions( &ImGuiEditorInternal::allocateForImGui, &ImGuiEditorInternal::freeForImGui, nullptr );
        ImGui::CreateContext();
        ImPlot::CreateContext();
        // 확장 DLL 의 ImGui 사본에 이 컨텍스트를 바인딩한다. 확장마다 CMake 가 만든 바인더를 쓴다. EditorModule 자신은 여기서 CreateContext 를 호출했다.
        EditorUIContext::publish( EditorUIContextState{ ImGui::GetCurrentContext(), ImPlot::GetCurrentContext() } );

        SW_LOG_TRACE( "Configuring ImGui IO" );
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // 멀티 뷰포트는 네 백엔드 모두에 렌더러 백엔드가 있으므로 항상 켠다. 렌더러 백엔드가 없는 백엔드라면 아래
        // createRendererBackend 가 nullptr 을 반환하고 초기화가 거기서 멈춘다.
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // 자체 테스트는 저장된 레이아웃을 읽지도 쓰지도 않는다. 도킹은 자체 테스트를 모르므로 셸이 알려 준다.
        _dockLayout.setPanelOverrideRequested( EditorSelfTestRunner::isRequested() );
        _dockLayout.initializePersistencePaths();
        _dockLayout.applyIniFilename();

        // 글꼴은 데이터(editortooldefaults.json 후보 · 테마 글자 크기)에서 온다. 못 찾으면 이유를 남겼고 기동을 멈춘다.
        if ( EditorFontSetup::apply() == false )
        {
            shutdownImGuiContext();
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownImGuiContext()
    {
        // 글꼴 아틀라스는 컨텍스트가 보관한다. 확장 DLL 의 사본에서 먼저 분리한다.
        EditorUIContext::publish( EditorUIContextState{} );
        if ( ImPlot::GetCurrentContext() != nullptr )
            ImPlot::DestroyContext();
        if ( ImGui::GetCurrentContext() != nullptr )
            ImGui::DestroyContext();
    }

    SubsystemInitResult ImGuiEditor::initializePlatformBackend()
    {
        SW_LOG_TRACE( "Creating Platform Backend" );
        _platformBackend = IImGuiPlatformBackend::createPlatformBackend();
        if ( _platformBackend == nullptr )
        {
            SW_LOG_ERROR( "Failed to create platform backend" );
            return SubsystemInitResult::Failed;
        }

        SW_LOG_TRACE( "Initializing Platform Backend" );
        if ( _platformBackend->initialize( _pWindow, _pRHIDevice->getBackendType() ) == false )
        {
            SW_LOG_ERROR( "Platform backend initialization failed" );
            shutdownPlatformBackend();
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownPlatformBackend()
    {
        if ( _platformBackend == nullptr )
            return;
        _platformBackend->shutdown();
        _platformBackend.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeRendererBackend()
    {
        SW_LOG_TRACE( "Creating Renderer Backend" );
        _rendererBackend = IImGuiRendererBackend::createRendererBackend( _pRHIDevice->getBackendType() );
        if ( _rendererBackend == nullptr || _rendererBackend->initialize( _pRHIDevice ) == false )
        {
            SW_LOG_ERROR( "Renderer backend initialization failed" );
            shutdownRendererBackend();
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownRendererBackend()
    {
        if ( _rendererBackend == nullptr )
            return;
        _rendererBackend->shutdown();
        _rendererBackend.reset();
    }

    SubsystemInitResult ImGuiEditor::loadSplashAssets()
    {
        SW_LOG_TRACE( "Splash: loading DefaultRenderPass / ForwardPipeline..." );

        const shared_ptr<RenderPassAsset>     defaultPass     = sw::make_shared<RenderPassAsset>();
        const shared_ptr<RenderPipelineAsset> forwardPipeline = sw::make_shared<RenderPipelineAsset>();

        TaskManager* pTaskManager = editor::findSubsystem<TaskManager>();
        if ( pTaskManager == nullptr )
        {
            SW_LOG_ERROR( "TaskManager service is not bound — cannot load splash resources" );
            return SubsystemInitResult::Failed;
        }

        const shared_ptr<ImGuiEditorInternal::SplashLoadResult> pResult = sw::make_shared<ImGuiEditorInternal::SplashLoadResult>();

        TaskHandle hDefault = pTaskManager->emplaceTask(
            "EditorSplash_DefaultRenderPass",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, ImGuiEditorInternal::loadSplashDefaultRenderPass ),
            MakeTaskArgs( defaultPass, pResult ) );

        TaskHandle hForward = pTaskManager->emplaceTask(
            "EditorSplash_ForwardPipeline",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, ImGuiEditorInternal::loadSplashForwardPipeline ),
            MakeTaskArgs( forwardPipeline, pResult ) );

        TaskStageHandle stage = pTaskManager->createStage( "EditorSplashLoad" );
        stage.addTask( hDefault ).addTask( hForward );

        hDefault.submit();
        hForward.submit();

        pTaskManager->waitStage( stage );
        // 두 파일은 엔진 필수 데이터다 — 하나라도 못 읽었으면(이유는 작업이 남겼다) 기동을 멈춘다.
        if ( pResult->_bRenderPassLoaded == false || pResult->_bForwardPipelineLoaded == false )
            return SubsystemInitResult::Failed;
        return SubsystemInitResult::Succeeded;
    }

    SubsystemInitResult ImGuiEditor::initializeEditorContext()
    {
        _editorContext = make_unique<EditorContext>();
        _editorContext->initialize();
        _editorContext->setDockLayout( &_dockLayout );
        _pUICapture = &_editorContext->getSubsystem<EditorUICapture>();
        // 테마는 컨텍스트가 활성화된 뒤에 읽어야 한다. 테마 상태를 컨텍스트가 보관하므로, 그 전에 부르면 적용한 테마가 갈 곳이
        // 없어 조용히 버려진다. 이때 stored preset 이 0 에 머문다. 설정한 벌이 없으면 다른 벌로 대신하지 않고 기동을 멈춘다(이유는 로그에 남았다).
        if ( EditorThemeUtil::loadFromConfig() == false )
        {
            shutdownEditorContext();
            return SubsystemInitResult::Failed;
        }
        _editorContext->getSubsystem<EditorRenderViews>().setRHIDevice( _pRHIDevice );
        _editorContext->getSubsystem<EditorRenderViews>().setRendererBackend( _rendererBackend.get() );

        _editorContext->getSubsystem<EditorPanelManager>().registerDefaultPanels();
        // 사용자 단축키(Saved/Editor/Shortcuts.json)는 레지스트리를 만들 때 적용한다. 처음 실행이면 파일이 없다.
        if ( EditorCommandGUI::loadShortcutOverrides() == EditorUserFileResult::Missing )
            SW_LOG_TRACE( "No shortcut overrides yet - default shortcuts" );
        EditorCommandGUI::registerDefaults();
        // 리로드 전에 기록한 오브젝트 편집을 되돌려도 이 모듈이 선택과 씬 dirty 상태를 맞춘다.
        CommandStack* pCommandStack = editor::findSubsystem<CommandStack>();
        if ( pCommandStack != nullptr )
            EditorTransaction::bindObjectEditListener( *pCommandStack );
        EditorExtensionRegistryDump::dumpIfRequested();
        _dockLayout.loadPanelVisibility();
        const bool bResumedSession = ImGuiEditorInternal::resumeEditSession( *_editorContext );

        // 모니터 DPI 에 맞춰 스타일과 글자를 키운다. 테마를 읽은 뒤에 해야 한다. 테마가 96 DPI 기준 크기를 기록하기 때문이다.
        // 모니터를 옮기면 글자와 플랫폼 창이 따라간다(ImGui 1.92 동적 폰트).
        // 배율을 직접 정했으면(`gv_editorUiScale`) 모니터를 옮겨도 글자 배율을 덮어쓰지 않는다.
        // 우선순위는 스위치(자동화), 환경설정(General 의 UI 배율), 모니터 DPI 순이다. 배율이 0 이면 다음 순위로 넘어간다.
        const float32 fixedUIScale  = gv_editorUiScale > 0.0f ? static_cast<float32>( gv_editorUiScale ) : getPreferences<EditorGeneralPreferences>()._uiScale;
        const bool    bFixedUIScale = fixedUIScale > 0.0f;
        EditorThemeUtil::setDpiScale( bFixedUIScale ? fixedUIScale : _platformBackend->getDpiScale() );
        ImGui::GetIO().ConfigDpiScaleFonts     = bFixedUIScale == false;
        ImGui::GetIO().ConfigDpiScaleViewports = bFixedUIScale == false;
        SW_LOG_INFO( "Editor UI scale %# (%#, frame padding %#x%#)", EditorThemeUtil::getDpiScale(), bFixedUIScale ? "fixed" : "monitor DPI",
                     ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y );

        // `-gv_editorStartupScene=<경로>` 는 검증용이다. 기동 검증이 오브젝트를 순회하는 코드까지 실행하게 한다. 정의는 이 파일 위쪽에 있다.
        // 스위치가 있으면 그 값을 쓰고, 없으면 환경설정(General 의 시작 씬)을 쓴다.
        // 앞선 인스턴스를 이어받았으면(백엔드 교체 · 에디터 모듈 리로드) 시작 씬을 열지 않는다 — 열면 편집 중인 씬과 미저장 편집을 버린다.
        const string startupScene = gv_editorStartupScene.empty() == false ? string( gv_editorStartupScene ) : getPreferences<EditorGeneralPreferences>()._startupScene;
        if ( startupScene.empty() == false && bResumedSession == false )
        {
            SW_LOG_INFO( "Opening the startup scene: %#", startupScene.c_str() );
            if ( EditorSceneFileCommands::loadScene( startupScene ) == false )
                SW_LOG_ERROR( "Failed to open the startup scene: %#", startupScene.c_str() );
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownEditorContext()
    {
        // Undo 스택에서는 이 모듈의 코드를 가진 명령과 편집 리스너만 제거한다. 오브젝트 편집은 엔진 데이터 명령이므로 남는다.
        // 호스트가 이미지를 내리지 않고 인스턴스만 다시 만들면(백엔드 교체) 모듈 명령도 남긴다 — 다음 인스턴스가 되돌릴 수 있다.
        const EditSessionState* pEditSession = editor::findSubsystem<EditSessionState>();
        if ( pEditSession == nullptr || pEditSession->isModuleImageKept() == false )
            ImGuiEditorInternal::releaseModuleUndoCommands();
        if ( _editorContext == nullptr )
            return;
        ImGuiEditorInternal::storeEditSession( *_editorContext );
        // 렌더 스레드는 이미 쉬고 있다(종료 경로가 렌더 워커를 비운 뒤다).
        _pUICapture->releaseRHI( _pRHIDevice );
        _pUICapture = nullptr;
        _editorContext->getSubsystem<EditorRenderViews>().destroyViewTargets();
        _editorContext->getSubsystem<EditorPanelManager>().shutdownAllPanels( nullptr );
        _editorContext->getSubsystem<EditorPanelManager>().clear();
        _editorContext->shutdown();
        _editorContext.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeWindowCloseQuery()
    {
        _pWindow->setCloseQueryHandler( SW_DELEGATE_METHOD( WindowCloseQueryDelegate, &ImGuiEditor::onWindowCloseQuery, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownWindowCloseQuery()
    {
        MainWindowSubsystem* pWindowHost   = editor::findSubsystem<MainWindowSubsystem>();
        IWindow*             pActiveWindow = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pActiveWindow == nullptr )
            return;
        pActiveWindow->setCloseQueryHandler( {} );
        // 에디터를 종료하면 창 제목은 팩 설정 그대로 돌아간다. 창 제목의 출처는 팩 설정(`<Engine _windowTitle>`) 하나뿐이다.
        const PackEngineSettings* pPackSettings = editor::findSubsystem<const PackEngineSettings>();
        if ( pPackSettings != nullptr )
            pActiveWindow->setTitle( pPackSettings->_windowTitle.c_str() );
    }

    void ImGuiEditor::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // 열려 있는 파일 대화상자의 결과 델리게이트를 해제한다. 모듈을 언로드하기 전에 제거해야 하는 이 DLL 안의 주소다.
        // 전역 변수는 모듈을 언로드하는 쪽이 모듈 이름으로 정리한다. 그 델리게이트는 이 DLL 안의 함수와 `this` 를 참조하고,
        // 네이티브 대화상자는 사용자가 닫을 때까지 떠 있다. Undo 스택과 같은 종류의 함정이다.
        FileUtil::cancelFileDialogResults();

        // 기다리지 않는다. 여기로 오는 경로(ModuleHost::suspendModules)는 이미 drainRenderWorkers 로 렌더 워커를 비운 뒤이므로
        // 기다릴 대상이 없다.
        abandonPendingDraw();
        for ( EditorDrawDataSnapshot& snapshot : _arrDrawSnapshot )
        {
            snapshot.clear();
        }

        _dockLayout.save();

        // 기동 단계를 초기화의 역순으로 종료하고 목록을 비운다(다시 initialize 할 수 있다).
        _startup.destroyAll();
        _pWindow      = nullptr;
        _pRHIDevice   = nullptr;
        _bInitialized = SW_FALSE;
    }

    void ImGuiEditor::preRender( IRHIDevice* pRHIDevice )
    {
        if ( _bInitialized == SW_FALSE || _editorContext == nullptr )
            return;

        _editorContext->getSubsystem<EditorPanelManager>().preRenderOpenPanels( pRHIDevice );
    }

    void ImGuiEditor::prepareFrameState()
    {
        if ( _editorContext == nullptr )
            return;
        // 프레임 앞의 상태 정리는 단계로 옮기지 않는다. 메뉴 · 패널이 이번 프레임에 읽을 값이라 프레임을 열기 전이어야 한다.
        // 확장 모듈은 에디터가 뜬 뒤에도 로드되고 언로드된다. 등록 세대가 바뀌었으면 인스턴스를 맞추고, 같으면 비교 한 번으로 끝난다.
        _editorContext->syncExtensionRegistrations();
        // 토큰 파일이 바뀌었으면(핫 리로드) 프레임을 열기 전에 다시 읽어 이번 프레임부터 새 스타일로 그린다.
        if ( _editorContext->getSubsystem<EditorTheme>().consumeTokensChanged() )
            EditorThemeUtil::reloadThemes();
#if !defined( SW_SHIPPING )
        // 기동 뒤에 올라온 확장 모듈의 환경설정 섹션은 기본값이다. 새 섹션이 없으면 목록을 한 번 훑고 끝난다.
        if ( EditorPreferencesStore::hasNewSections() )
            (void)EditorPreferencesStore::loadNewSections( EditorPreferencesStore::getDefaultFilePath() ); // 입힌 수는 쓰지 않는다
#endif
        _editorContext->getSubsystem<EditorRenderViews>().setGameViewFocused( false );
        _editorContext->getSubsystem<EditorRenderViews>().setGameViewHovered( false );
        _editorContext->getSubsystem<EditorRenderViews>().clearViewDrawnMarks();
    }

    void ImGuiEditor::openFrame()
    {
        // 프레임 열기는 단계로 옮기지 않는다. 메뉴 · 툴바는 도크스페이스보다 먼저 그려야 도크 영역이 그만큼 줄고, 도크스페이스는 어느 단계보다 먼저 열려야
        // 패널이 그 안에 도킹된다. 그래서 순서가 ImGui 가 정한 것이고 셸이 소유한 도킹 레이아웃(`_dockLayout`)을 쓴다.
        SW_EDITOR_PROFILE_SCOPE( "GT.Editor.newFrame" );
        beginFrame();
        EditorMenuBar::drawThemeDialog();
        EditorMenuBar::draw( _dockLayout );
        EditorPlayToolbar::draw(); // 메뉴 바 아래에 둔다. 도크스페이스보다 먼저 그려야 도크 영역이 그만큼 줄어든다
        _dockLayout.beginDockspace();
    }

    void ImGuiEditor::closeAndPublishFrame()
    {
        // 아래는 단계로 옮기지 않는다. ImGui 프레임을 닫고(Render) draw 스냅샷을 렌더 스레드에 넘기는 셸의 약속이라, 위치가 곧 뜻이다.
        SW_EDITOR_PROFILE_SCOPE( "GT.Editor.endFrame" );
        {
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.render" );
            endFrame();
        }

        // EndFrame 이후에 호출해야 창의 DrawList 에 이번 프레임의 최종 내용이 담긴다.
        // -gv_editorPanelDump=N 이 없으면 아무것도 하지 않는다.
        EditorPanelDump::dumpIfRequested();

        // GL 처럼 컨텍스트가 렌더 스레드 전용이면 GPU 작업(텍스처 갱신, 보조 뷰포트 렌더)을
        // present 훅으로 옮긴다. 그 외 백엔드는 여기 UI 스레드에서 처리한다.
        const bool bRenderThreadContext = _rendererBackend != nullptr && _rendererBackend->requiresRenderThreadContext();

        // ImGui 1.92 동적 아틀라스는 폰트와 텍스처의 생성과 갱신을 그리기 전에 마쳐야 한다.
        // 메인 스냅샷은 Textures==nullptr 로 넘겨, 렌더 스레드가 이 리스트를 건드리지 않게 한다.
        if ( _rendererBackend != nullptr && bRenderThreadContext == false )
            _rendererBackend->processTextureUpdates();

        ImGuiIO& io = ImGui::GetIO();
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            // 플랫폼(OS 창) 갱신은 항상 UI 스레드에서 한다. imgui 1.92 뷰포트 관리는 단일 스레드 호출을 전제하므로, 보조(플로팅)
            // 뷰포트의 GPU 렌더와 present 는 한 스레드에서만 실행해야 한다. GL 에서는 그 스레드가 렌더 스레드다(아래 render() 에서 처리).
            // 창 생성, 크기 변경, 파괴와 렌더가 GPU 큐에 제출하고 대기하므로 렌더러 백엔드의 큐 락 안에서 실행한다.
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.platformWindows" );
            const std::unique_lock<mutex> queueLock = ( _rendererBackend != nullptr ) ? _rendererBackend->lockSubmissionQueue() : std::unique_lock<mutex>{};
            ImGui::UpdatePlatformWindows();
            if ( bRenderThreadContext == false )
                ImGui::RenderPlatformWindowsDefault();
        }

        const uint32 writeSlot = _drawSnapshotHandoff.waitForWriteSlot();

        // 스냅샷을 내기 전에 번호를 알린다. 이 뒤에 해제하는 자원은 이 스냅샷이 그릴 수 있으므로 다음 번호를 받아야 한다.
        ++_lastDrawSnapshotSequence;
        {
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.captureDrawSnapshot" );
            // 에디터 UI 캡처 요청이 있으면 이 프레임의 모든 뷰포트 합본도 넘긴다. 잘림 지표는 창 상태가 남아 있는 지금 측정한다.
            EditorUICaptureJob captureJob{};
            const bool         bCapture = _pUICapture != nullptr && _pUICapture->takeJob( captureJob );
            _arrDrawSnapshot[writeSlot].capture( _lastDrawSnapshotSequence, bCapture ? &captureJob : nullptr );
        }
        if ( _rendererBackend != nullptr )
            _rendererBackend->getDrawReleaseQueue().markSnapshotPublished( _lastDrawSnapshotSequence );
        // 게시하면 이 프레임이 "렌더 대기" 가 된다. 다음 updateUI 는 상단의 waitForDrawSnapshotIdle 에서 postPresent 까지 막힌다.
        // 그래서 렌더 스레드가 present 훅에서 ImGui 공유 상태(텍스처 리스트, 뷰포트)를 건드리는 GL 경로에서도 UI 스레드와 겹치지 않는다.
        _drawSnapshotHandoff.publish( writeSlot );
    }

    void ImGuiEditor::updateUI()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // 하위 구간은 `GT.Editor.updateUI`(App) 의 시간이 어디에 쓰이는지 나눠 보여 준다. 패널별 시간은 `-gv_editorPanelTimes=N` 으로 본다.
        {
            // 지난 UI 프레임을 렌더 스레드가 다 그릴 때(postPresent)까지 기다린다. 이 값이 크면 UI 작업이 아니라 RT, GPU, Present 대기가 길다는 뜻이다.
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.waitDrawSnapshot" );
            waitForDrawSnapshotIdle();
        }

        prepareFrameState();
        openFrame();

        // 프레임 안의 일(단축키 → 에셋 리로드 → 패널 → 자체 테스트 → 창 제목 → 스크린샷)은 등록된 단계가 `_order` 순서로 한다(`EditorFrameStep.h`).
        // 셸은 단계를 모른다. 새 단계는 자기 .cpp 의 `SW_EDITOR_FRAME_STEP` 한 줄이다. 셸이 아는 단계는 `EditorFrameSteps.cpp` 에 있다.
        if ( _editorContext != nullptr )
        {
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.frameSteps" );
            ImGuiEditorInternal::runFrameSteps( *_editorContext );
        }

        closeAndPublishFrame();
    }

    void ImGuiEditor::render( IRHIDevice* pRHIDevice )
    {
        if ( _bInitialized == SW_FALSE || pRHIDevice == nullptr )
            return;

        // GL 에서는 컨텍스트가 이 스레드(렌더 스레드)에 바인딩된 지금이 프레임 GPU 작업을 할 수 있는 유일한 지점이다.
        const bool bRenderThreadContext =
            _rendererBackend != nullptr && _rendererBackend->requiresRenderThreadContext();
        if ( bRenderThreadContext )
        {
            _rendererBackend->newFrame();
            _rendererBackend->processTextureUpdates();
        }

        const uint32 slot = _drawSnapshotHandoff.beginRender();
        if ( slot < constant::kMaxFrameCountInFlight )
        {
            ImDrawData* pDrawData = _arrDrawSnapshot[slot].getMainDrawData();
            if ( pDrawData != nullptr )
                renderBackend( pRHIDevice, pDrawData );

            // 에디터 UI 캡처: 같은 프레임의 모든 뷰포트 합본을 캡처 텍스처에 한 번 더 그린다. GL 의 플로팅 창 렌더(아래)보다 먼저다 — 그것은 컨텍스트를 바꾼다.
            // `renderJob` 은 자기가 연 패스를 닫고 돌아온다(GL 은 기본 프레임버퍼로 되돌아온다). 아래 플로팅 창이 자기 창에 그리려면 그래야 한다.
            // 합본의 주인은 메인 뷰포트라 백엔드의 메인 버퍼 링을 한 슬롯 더 쓴다. 링 크기가 이 횟수를 센다(`kImGuiDrawsPerViewportPerFrame`) — 그리는 횟수를 늘리면 그 값도 올린다.
            ImDrawData* const pCaptureDrawData = _arrDrawSnapshot[slot].findCaptureDrawData();
            const bool        bCapture         = pCaptureDrawData != nullptr && _pUICapture != nullptr && _rendererBackend != nullptr;
            if ( bCapture )
                _pUICapture->renderJob( *pRHIDevice, *_rendererBackend, *pCaptureDrawData, _arrDrawSnapshot[slot].getCaptureJob() );

            // 이 프레임의 스냅샷보다 먼저 해제 요청된 자원은 앞 프레임들만 그렸다. 이 프레임의 GPU 작업이 끝난 뒤에 해제되도록 디바이스로 넘긴다.
            if ( _rendererBackend != nullptr )
                _rendererBackend->getDrawReleaseQueue().handOverToDevice( *pRHIDevice, _arrDrawSnapshot[slot].getSequence() );
        }

        // 보조(플로팅) 뷰포트도 GL 이면 여기 렌더 스레드에서 렌더하고 present 한다.
        // UI 스레드는 updateUI 상단의 waitForDrawSnapshotIdle 에서 막혀 있으므로 ImGui 상태가 안정적이다.
        if ( bRenderThreadContext )
        {
            const ImGuiIO& io = ImGui::GetIO();
            if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
                ImGui::RenderPlatformWindowsDefault();
        }
    }

    void ImGuiEditor::postPresent( IRHIDevice* pRHIDevice )
    {
        // endFrame 뒤라 이 프레임에 그린 캡처를 읽을 수 있다(GPU 를 기다린다 — 캡처한 프레임만).
        if ( _pUICapture != nullptr && pRHIDevice != nullptr )
            _pUICapture->finishJob( *pRHIDevice );
        // 메인 스냅샷 렌더가 끝났으므로 UI 스레드가 다음 슬롯을 쓸 수 있게 해제한다.
        // 보조 뷰포트는 updateUI 에서 UI 스레드가 이미 렌더하고 present 했다.
        _drawSnapshotHandoff.endRender();
    }

    void ImGuiEditor::abandonPendingDraw()
    {
        // in-flight 표시는 "렌더 스레드가 이 슬롯을 읽고 postPresent 에서 해제한다" 는 약속이다. 에디터 인스턴스를 없애면(destroyEditorInstance)
        // 그 약속을 지킬 쪽이 사라지므로 여기서 푼다. 풀지 않으면 shutdown 뒤의 waitForDrawSnapshotIdle 이 영원히 반환하지 않는다.
        // 인스턴스가 살아 있는 동안(게임 모듈만 리로드할 때)에는 부르지 않는다. 그 표시는 아직 렌더 스레드에 제출되지 않은 프레임의 것이다.
        _drawSnapshotHandoff.abandon();
    }

    bool ImGuiEditor::processEvent( const NativeWindowEvent& event )
    {
        if ( _bInitialized == SW_FALSE )
            return false;

        if ( _platformBackend != nullptr )
        {
            // 셸이 이미 장치 이벤트를 버리지만(`InputManager::shouldDropNativeEvent`), 비클라이언트 이벤트와 떠남 이벤트(WM_NCMOUSEMOVE, WM_MOUSELEAVE)도 커서 위치를 넣는다.
            const ScopedImGuiOsInputFilter osInputFilter{ ImGuiEditorInternal::isOsDeviceInputBlocked() };
            _platformBackend->processEvent( event );
        }

        // ImGui 가 가져간 입력은 게임으로 넘기지 않는다. 게임 뷰 위에서는 예외다. 씬 뷰 위의 입력은 에디터 카메라가 처리한다.
        const ImGuiIO& io               = ImGui::GetIO();
        const bool     bGameViewHovered = _editorContext != nullptr && _editorContext->getSubsystem<EditorRenderViews>().isGameViewHovered();
        const bool     bGameViewFocused = _editorContext != nullptr && _editorContext->getSubsystem<EditorRenderViews>().isGameViewFocused();

        // Simulate 는 월드만 실행하므로 게임 입력을 주지 않는다. 에디터 카메라와 패널은 위의 ImGui 처리에서 이미 입력을 받았다.
        if ( EditorPlaySession::isSimulating() && ( event.isMouseInput() || event.isKeyboardInput() ) )
            return true;

        if ( event.isMouseInput() )
        {
            if ( io.WantCaptureMouse && bGameViewHovered == false )
                return true;
            return false;
        }

        if ( event.isKeyboardInput() )
        {
            const bool bKeyboardCapturedByEditor = event.isInputRelease() == false && io.WantCaptureKeyboard && bGameViewFocused == false;
            if ( bKeyboardCapturedByEditor )
                return true;
        }

        return false;
    }

    void ImGuiEditor::getGameViewport( HostViewTarget* pOutTarget ) const
    {
        const bool bRequest = _editorContext != nullptr && EditorViewTargetUtil::shouldRequestGameView( _editorContext->getSubsystem<EditorRenderViews>().wasViewDrawn( EditorViewKind::Game ) );
        ImGuiEditorInternal::writeViewport( bRequest ? &_editorContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Game ) : nullptr, pOutTarget );
    }

    void ImGuiEditor::getSceneViewport( HostViewTarget* pOutTarget ) const
    {
        const bool bRequest = _editorContext != nullptr && EditorViewTargetUtil::shouldRequestSceneView( _editorContext->getSubsystem<EditorRenderViews>().wasViewDrawn( EditorViewKind::Scene ),
                                                                                                         _editorContext->getSubsystem<EditorRenderViews>().wasViewDrawn( EditorViewKind::Game ) );
        ImGuiEditorInternal::writeViewport( bRequest ? &_editorContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Scene ) : nullptr, pOutTarget );
    }

    uint32 ImGuiEditor::getSceneViews( HostSceneView* pOutView, uint32 capacity ) const
    {
        if ( _editorContext == nullptr || pOutView == nullptr )
            return 0;
        return _editorContext->getSubsystem<EditorRenderViews>().collectSceneViews( pOutView, capacity );
    }

    CameraComponent* ImGuiEditor::getSceneViewCamera() const
    {
        // 씬 뷰는 Play 와 Simulate 중에도 에디터 카메라를 쓴다. 게임 카메라 출력은 게임 뷰가 그린다.
        return EditorCamera::ensure( editor::getActiveScene() );
    }

    bool ImGuiEditor::isPlaying() const
    {
        // 호스트는 이 반환값으로 게임 모듈 업데이트를 켠다. Simulate 는 씬 틱만 실행하고(isPaused 가 false) 게임 모듈은 실행하지 않는다.
        return EditorPlaySession::isPlayerActive();
    }

    bool ImGuiEditor::isPaused() const
    {
        return EditorPlaySession::isPaused() && EditorPlaySession::hasPendingStep() == false;
    }

    void ImGuiEditor::stopSimulation()
    {
        EditorPlaySession::stop();
    }

    void ImGuiEditor::onHostFrameEnd()
    {
        // 씬을 여는 중에 누른 Play 는 미뤄 둔다. 로드가 끝난 프레임에 여기서 시작한다.
        EditorPlaySession::update();
        EditorPlaySession::consumePendingStep();
        EditorPlayCommands::update(); // Maximize On Play. 시작과 정지가 어디서 왔든 상태 전이를 여기서 확인한다
    }

    bool ImGuiEditor::onWindowCloseQuery()
    {
        return EditorSceneFileCommands::tryBeginQuit();
    }

    void ImGuiEditor::beginFrame()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // GL 처럼 컨텍스트가 렌더 스레드 전용인 백엔드는 newFrame 을 present 훅에서 호출한다.
        if ( _rendererBackend != nullptr && _rendererBackend->requiresRenderThreadContext() == false )
            _rendererBackend->newFrame();

        if ( _platformBackend != nullptr )
        {
            // 무인 자동화 중이면 플랫폼이 조회한 실제 커서를 지운다. Win32 는 GetCursorPos 와 커서 아래 뷰포트, X11 은 XQueryPointer 의 위치와 버튼이 해당한다.
            // 테스트가 커서를 놓은 프레임(버튼을 놓는 프레임)에 실제 커서가 마지막 위치가 되면 ImGui 가 버튼 놓기를 다음 프레임으로 미뤄 뷰포트 비행이 그 차이만큼 돈다.
            const ScopedImGuiOsInputFilter osInputFilter{ ImGuiEditorInternal::isOsDeviceInputBlocked() };
            _platformBackend->newFrame();
        }
        // 자체 테스트가 만든 가상 입력은 플랫폼 백엔드가 넣은 실제 커서보다 뒤에 넣어야 우선한다. ImGui 는 큐를 순서대로 처리하기 때문이다.
        EditorSelfTestInput::flushIntoImGui();

        // 이름 붙인 레이아웃은 프레임 밖에서 읽어야 이미 있는 창과 도킹 노드에 적용된다.
        _dockLayout.applyPendingNamedLayout();
        // 플랫폼 백엔드는 io.DeltaTime 에 벽시계 경과를 적는다. 패널이 각자 우회하지 않도록 ImGui 경과를 엔진 경과로 맞춘다.
        // 자동화에서는 엔진 경과가 고정 프레임 시간이라 녹화와 재생이 같은 시간을 읽는다. 벽시계가 필요한 프로파일러 성능 탭은 자기 시계를 읽는다.
        ImGui::GetIO().DeltaTime = MathUtil::max( GameTimeScale::getUnscaledDeltaTime(), ImGuiEditorInternal::kMinDeltaTime );
        ImGui::NewFrame();
        // 모니터를 옮기면 ImGui 가 글자 배율(FontScaleDpi)만 새 DPI 로 덮어쓴다(ConfigDpiScaleFonts). 테마의 여백과 둥글기는 이전 배율이 남으므로 따라가게 한다.
        // 그러지 않으면 150 % 모니터에서 글자만 커지고 위젯 크기는 그대로다. 배율을 직접 정했으면(gv_editorUiScale) ImGui 가 덮어쓰지 않으므로 여기서도 건드리지 않는다.
        // 이 프레임의 위젯은 아직 그리지 않았다. 스타일 크기는 그릴 때 읽힌다.
        {
            const float32 fontScaleDpi = ImGui::GetStyle().FontScaleDpi;
            const bool    bDpiMoved    = ImGui::GetIO().ConfigDpiScaleFonts && fontScaleDpi > 0.0f &&
                                   MathUtil::abs( fontScaleDpi - EditorThemeUtil::getDpiScale() ) > ImGuiEditorInternal::kDpiFollowTolerance;
            if ( bDpiMoved )
                EditorThemeUtil::setDpiScale( fontScaleDpi );
        }
        // 실행 중에 `gv_editorUiScale` 을 바꾸면(콘솔, 시나리오 `<Variable>`, gv 패널) 바로 반영한다. 0 으로 되돌리면 다시 모니터 DPI 를 따른다.
        {
            const bool    bFixedUIScale = gv_editorUiScale > 0.0f;
            const float32 wantedScale   = bFixedUIScale ? static_cast<float32>( gv_editorUiScale ) : _platformBackend != nullptr ? _platformBackend->getDpiScale()
                                                                                                                                 : 1.0f;
            const bool    bModeChanged  = ImGui::GetIO().ConfigDpiScaleFonts == bFixedUIScale;
            const bool    bFixedMoved   = bFixedUIScale && MathUtil::abs( wantedScale - EditorThemeUtil::getDpiScale() ) > ImGuiEditorInternal::kDpiFollowTolerance;
            if ( bModeChanged || bFixedMoved )
            {
                ImGui::GetIO().ConfigDpiScaleFonts     = bFixedUIScale == false;
                ImGui::GetIO().ConfigDpiScaleViewports = bFixedUIScale == false;
                EditorThemeUtil::setDpiScale( wantedScale );
                SW_LOG_INFO( "Editor UI scale %# (%#)", EditorThemeUtil::getDpiScale(), bFixedUIScale ? "fixed" : "monitor DPI" );
            }
        }
        ImGuizmo::BeginFrame();
        // 기즈모를 띄우는 패널이, 캔버스가 입력을 받을 수 있을 때 다시 켠다. 끌고 있는 동안은 끄지 않는다. Enable( false ) 는 끌기 상태(mbUsing)까지
        // 지우므로, 프레임마다 끄면 누른 다음 프레임에 끌기가 끊겨 기즈모가 움직이지 못한다.
        if ( ImGuizmo::IsUsing() == false )
            ImGuizmo::Enable( false );
    }

    void ImGuiEditor::endFrame()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 0.0f );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, EditorColorUtil::toImVec4( ImGuiEditorInternal::kNotificationWindowBg ) );
        ImGui::RenderNotifications();
        ImGui::PopStyleColor( 1 );
        ImGui::PopStyleVar( 2 );

        ImGui::Render();
    }

    void ImGuiEditor::waitForDrawSnapshotIdle()
    {
        _drawSnapshotHandoff.waitForIdle();
    }

    void ImGuiEditor::renderBackend( IRHIDevice* pRHIDevice, ImDrawData* pDrawData )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( _rendererBackend != nullptr )
            _rendererBackend->render( pRHIDevice, pDrawData );
    }
} // namespace sw::editor

// ==============================================================================
// EditorModule C-ABI 진입점(매크로가 구현한다)
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::editor::ImGuiEditor );
