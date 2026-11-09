#include "pch.h"

#include "Editor/ImGuiEditor.h"

#include "Core/Diagnostics/SubsystemTimingReport.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Backend/IImGuiPlatformBackend.h"
#include "Editor/Common/Backend/ImGuiOSInputFilter.h"
#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Commands/Play/EditorPlayCommands.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/Commands/Scene/EditorLightProbeBakeSession.h"
#include "Editor/Common/Commands/Scene/EditorReflectionProbeBakeSession.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Config/EditorAppearanceConfig.h"
#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/EditorPlayToolbar.h"
#include "Editor/Common/GUI/EditorUIContextState.h"
#include "Editor/Common/GUI/Panel/EditorPanelDump.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorFontSetup.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Task/EditorBackgroundTask.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"
#include "Editor/EditorModuleExports.h"
#include "Editor/EditorSubsystemSet.h"
#include "Editor/SelfTest/EditorExtensionRegistryDump.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"
#include "Engine/Window/NativeWindowEvent.h"

#include "ModuleAPI/ABI/EditorAPI.h"
#include "ModuleAPI/Service/IModuleStateStore.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <implot.h>
#include <imgui_internal.h>

// EditorRedrawUtil — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
namespace sw::editor
{
    /** @struct EditorRedrawUtil @brief 다시 그릴 까닭을 모으는 정적 함수 모음입니다. */
    struct EditorRedrawUtil
    {
        /**
         * @brief ImGui 입력 · 잡은 항목 · 플레이 · 알림 · 패널의 연속 프레임 요청을 모아 `EditorRedrawReason` 비트 묶음으로 냅니다. 에디터 UI 갱신 뒤에 부릅니다.
         * @return 에디터 컨텍스트나 ImGui 컨텍스트가 아직 없으면(그릴 에디터 UI 가 없다) 0 입니다.
         */
        [[nodiscard]] static uint32 collectReasons();
    };
} // namespace sw::editor

namespace sw::editor
{
    uint32 EditorRedrawUtil::collectReasons()
    {
        EditorContext* const      pContext = editor::findSubsystem<EditorContext>();
        const ImGuiContext* const pImGui   = ImGui::GetCurrentContext();
        if ( pContext == nullptr || pImGui == nullptr )
            return static_cast<uint32>( EditorRedrawReason::None ); // 그릴 에디터 UI 가 없다
        // 이번 프레임의 NewFrame 이 처리한 입력(마우스 위치 · 단추 · 키 · 글자 · 포커스)과 누르고 있는 단추.
        const bool bInput = pImGui->InputEventsTrail.Size > 0 || ImGui::IsAnyMouseDown();
        // 잡고 있는 것 — 다음 프레임 입력이 없어도 화면이 따라가야 한다(끌기 미리보기 · 창 옮기기 · Ctrl+Tab 창 고르기).
        const bool bActive  = ImGui::IsAnyItemActive() || pImGui->MovingWindow != nullptr || pImGui->DragDropActive || pImGui->NavWindowingTarget != nullptr;
        const bool bRunning = ( EditorPlaySession::isPlayerActive() || EditorPlaySession::isSimulating() ) && EditorPlaySession::isPaused() == false;
        const bool bToast   = pContext->getSubsystem<EditorNotificationManager>().getNotifications().empty() == false;
        const bool bAnimate = pContext->getSubsystem<EditorRenderViews>().isAnimationRequested();
        // 프레임 단계가 나르는 일 — 워커가 끝내도 창 메시지가 오지 않으므로 끝날 때까지 깨어 있는다.
        const EditorLightProbeBakeSession* const      pLightBake        = editor::findSubsystem<EditorLightProbeBakeSession>();
        const EditorReflectionProbeBakeSession* const pReflectionBake   = editor::findSubsystem<EditorReflectionProbeBakeSession>();
        const bool                                    bLightBaking      = pLightBake != nullptr && pLightBake->isRunning();
        const bool                                    bReflectionBaking = pReflectionBake != nullptr && pReflectionBake->isRunning();
        const bool                                    bWorking          = pContext->getSubsystem<AssetHotReload>().isImportBusy() || bLightBaking || bReflectionBaking;
        // 엔진이 비동기로 나르는 일 — 도착하면 화면이 바뀌지만 창 메시지는 오지 않는다(시나리오가 기다리는 `Assets.Pending` 과 같은 판정).
        SceneManager&      sceneManager = editor::getSubsystem<SceneManager>();
        const Scene* const pScene       = sceneManager.getActiveScene();
        const bool         bLoading     = sceneManager.isTransitioning() || CaptureReadiness::countPendingWork( pScene != nullptr ? pScene->getObjectManager() : nullptr ) > 0;
        uint32             reasons      = 0;
        reasons |= bInput ? static_cast<uint32>( EditorRedrawReason::Input ) : 0u;
        reasons |= bActive ? static_cast<uint32>( EditorRedrawReason::ActiveItem ) : 0u;
        reasons |= bRunning ? static_cast<uint32>( EditorRedrawReason::Simulation ) : 0u;
        reasons |= bToast ? static_cast<uint32>( EditorRedrawReason::Notification ) : 0u;
        reasons |= bAnimate ? static_cast<uint32>( EditorRedrawReason::Animation ) : 0u;
        reasons |= bWorking ? static_cast<uint32>( EditorRedrawReason::BackgroundJob ) : 0u;
        reasons |= bLoading ? static_cast<uint32>( EditorRedrawReason::AssetsLoading ) : 0u;
        return reasons;
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct ImGuiEditorInternal
        {
            /** @brief ImGui 백엔드의 보조 창 띄우기(`Platform_ShowWindow`)와 메인 창입니다 — `showPlatformWindow` 가 감싼다. */
            static inline void ( *s_pfnShowPlatformWindow )( ImGuiViewport* ) = nullptr;
            static inline const IWindow* s_pMainWindow                        = nullptr;

            /**
             * @brief 메인 창이 숨어 있으면(무인 실행 — 언리얼 -RenderOffScreen, `AutomationSession::applyPresentPolicy`) 보조 창(메인 밖으로 나간 툴팁 · 팝업 · 떼어 낸 패널)도
             *        띄우지 않습니다. 그리기는 같다. 두 플랫폼 백엔드(Win32 · X11)가 같은 규칙을 받도록 백엔드가 아니라 여기서 감싼다.
             */
            static void showPlatformWindow( ImGuiViewport* pViewport )
            {
                if ( s_pMainWindow == nullptr || s_pMainWindow->isVisibleRequested() )
                    s_pfnShowPlatformWindow( pViewport );
            }

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

            /// @brief 글자 배율(모니터 DPI)과 테마 배율이 이 값보다 크게 다르면 테마 배율을 글자 배율에 맞춘다(beginFrame).
            static constexpr float32 kDPIFollowTolerance = 0.001f;

            /// @brief ImGui 는 io.DeltaTime 이 0 이하이면 단언한다. 엔진 경과가 0 인 프레임(일시 정지 직후)에도 이 값 아래로 내리지 않는다.
            static constexpr float32 kMinDeltaTime = 0.00001f;

            /** @brief 무인 자동화가 사람의 장치 입력을 막는 중이면 true 입니다. 이때 플랫폼 백엔드가 ImGui 에 넣는 실제 커서와 키를 지웁니다. */
            static bool isOSDeviceInputBlocked()
            {
                return editor::getSubsystem<const InputManager>().isOSDeviceInputBlocked(); // 필수 - 에디터 프레임마다 묻는다
            }

            /** @brief 뷰의 렌더 타깃 핸들 · 크기 · 창 안 자리를 호스트 형식으로 씁니다. @p pView 가 없으면 빈 값이며, 이번 프레임에 그 뷰를 그리지 않았다는 뜻입니다. */
            static void writeViewport( const EditorViewTarget* pView, ViewportTarget* pOutTarget )
            {
                if ( pOutTarget == nullptr )
                    return;
                *pOutTarget = ViewportTarget{};
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

            /** @brief 맡김 칸(`IModuleStateStore`)에 둘 세션 상태의 키와 형식 판입니다. */
            static constexpr string_view kSessionStoreKey = "editor.session";
            static constexpr uint32      kSessionVersion  = 1;

            /** @brief 인스턴스가 종료하기 전에 선택 · 미저장 표시를 호스트의 맡김 칸에 맡깁니다. 다음 인스턴스가 `resumeEditSession` 으로 찾아갑니다. */
            static void storeEditSession( EditorContext& context )
            {
                // 선택 조회(②): 종료 경로라 호스트가 서비스 결합을 먼저 풀었을 수 있다. 그때는 다음 인스턴스가 이어받을 상태가 없다.
                IModuleStateStore* const pStore = editor::findSubsystem<IModuleStateStore>();
                if ( pStore == nullptr )
                    return;
                const EditorWorkspace& workspace = context.getSubsystem<EditorWorkspace>();
                const EditorSelection& selection = editor::getMainSceneSelection();
                vector<uint64>         listSelectedObjectID;
                for ( const GameObjectHandle& handle : selection.getSelectedHandles() )
                {
                    listSelectedObjectID.push_back( handle.objectID() );
                }
                // 워크스페이스가 아직 이 세대를 보지 못했으면 선택 · 표시는 옛 씬의 것이다 — 세대를 그대로 적어 다음 인스턴스가 버리게 한다.
                Archive archive;
                archive << kSessionVersion << workspace.getObservedSceneGeneration() << workspace.isSceneDirty();
                writeUndoArg( archive, listSelectedObjectID );
                writeUndoArg( archive, selection.getSelectedAssets() );
                pStore->store( kSessionStoreKey, vector<uint8>( archive.getData(), archive.getData() + archive.getSize() ) );
            }

            /**
             * @brief 앞선 인스턴스가 맡긴 선택 · 미저장 표시를 되찾습니다. 앞선 인스턴스가 있었으면 true 입니다(씬이 그 뒤에 바뀌었으면 값은 버리지만 true).
             * @details 워크스페이스는 지금 씬 세대를 이미 본 것으로 둔다 — 새 인스턴스가 지금 씬을 "새 씬" 으로 여겨 되돌리기 기록을 비우지 않게 한다
             *          (`EditorSceneGenerationSync`). 씬 오브젝트는 같은 id 로 이어지므로(게임 상태 복원 · 같은 씬) 선택 id 와 되돌리기 명령이 그대로 맞는다.
             */
            static bool resumeEditSession( EditorContext& context )
            {
                const uint64     generation = editor::getSubsystem<SceneManager>().getSceneGeneration();
                EditorWorkspace& workspace  = context.getSubsystem<EditorWorkspace>();
                workspace.setObservedSceneGeneration( generation );
                // 선택 조회(②): 맡김 칸이 없는 호스트(테스트 · 도구)는 이어받을 것이 없다.
                IModuleStateStore* const pStore = editor::findSubsystem<IModuleStateStore>();
                vector<uint8>            bytes;
                if ( pStore == nullptr || pStore->take( kSessionStoreKey, bytes ) == false )
                    return false;
                Archive        archive( bytes.data(), bytes.size() );
                uint32         version{ 0 };
                uint64         storedGeneration{ 0 };
                bool           bSceneDirty{ false };
                vector<uint64> listSelectedObjectID;
                vector<string> listSelectedAsset;
                archive >> version >> storedGeneration >> bSceneDirty;
                const bool bRead = archive.isOk() && version == kSessionVersion && readUndoArg( archive, listSelectedObjectID ) && readUndoArg( archive, listSelectedAsset );
                if ( bRead == false || storedGeneration != generation )
                    return true; // 앞선 인스턴스는 있었다 — 씬이 바뀌었거나 형식이 달라 값만 버린다
                if ( bSceneDirty )
                    workspace.markSceneDirty();
                vector<GameObject*> listObject;
                for ( const uint64 objectID : listSelectedObjectID )
                {
                    if ( GameObject* pObject = editor::findGameObject( GameObjectHandle::make( objectID ) ) )
                        listObject.push_back( pObject );
                }
                EditorSelection& selection = editor::getMainSceneSelection();
                if ( listObject.empty() == false )
                    selection.selectObjects( listObject, SelectionMode::Replace );
                if ( listSelectedAsset.empty() == false )
                    selection.selectAssets( listSelectedAsset, SelectionMode::Replace );
                SW_LOG_INFO_ID( "ImGuiEditor.ResumedEditSessionSelected", "Resumed the edit session: %# selected object(s), %#", static_cast<uint32>( listObject.size() ),
                                bSceneDirty ? "unsaved changes" : "no unsaved changes" );
                return true;
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

        SubsystemInitResult        initializeSubsystem() override { return _editor.initializeEditorData(); }
        void                       shutdownSubsystem() override { _editor.shutdownEditorData(); }
        const utf8*                getSubsystemName() const override { return kSubsystemName; }
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

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
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

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
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

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
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

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
            static constexpr const utf8* kArrDependency[] = { EditorDataSubsystem::kSubsystemName, ImGuiContextSubsystem::kSubsystemName, PlatformBackendSubsystem::kSubsystemName, RendererBackendSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

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

        SubsystemInitResult initializeSubsystem() override
        {
            // 창의 닫기 확인 칸은 주인이 하나다 — 이 단계가 구독을 쥐고, 내려갈 때 이 에디터가 건 것만 놓는다(창이 먼저 사라졌으면 구독은 이미 무효다).
            _closeQuery = _editor._pWindow->bindCloseQueryHandler( SW_DELEGATE_METHOD( WindowCloseQueryDelegate, &ImGuiEditor::onWindowCloseQuery, &_editor ) );
            return _editor.initializeWindowCloseQuery();
        }
        void shutdownSubsystem() override
        {
            _closeQuery.reset();
            _editor.shutdownWindowCloseQuery();
        }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override
        {
            static constexpr const utf8* kArrDependency[] = { EditorContextSubsystem::kSubsystemName };
            return kArrDependency;
        }
        SubsystemHostMask          getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Editor;

    private:
        ImGuiEditor& _editor;
        Subscription _closeQuery; ///< 창의 닫기 확인 칸 구독
    };
} // namespace sw::editor

namespace sw::editor
{
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
        , _renderer{ nullptr }
        , _editorToolDefaults{ nullptr }
        , _pIconGlyphTable{ nullptr }
        , _fontSetup{}
        , _pComponentIconTable{ nullptr }
        , _editorContext{ nullptr }
        , _dockLayout{}
        , _arrDrawSnapshot{}
        , _drawSnapshotHandoff{}
        , _lastDrawSnapshotSequence{ 0 }
        , _pUICapture{ nullptr }
        , _listFrameDrawData{}
        , _renderSlot{ DrawSnapshotHandoff::kInvalidSlot }
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
        SW_LOG_TRACE_ID( "ImGuiEditor.InitializeStart", "Initialize start." );
        if ( _bInitialized != SW_FALSE )
            return true;

        if ( pWindow == nullptr || pRHIDevice == nullptr )
        {
            SW_LOG_ERROR_ID( "ImGuiEditor.CannotInitializeWithoutWindow", "Cannot initialize without window and RHI device." );
            return false;
        }
        _pWindow    = pWindow;
        _pRHIDevice = pRHIDevice;

        // 단계마다 먼저 서야 하는 단계를 적는다. 목록이 그 순서로 초기화하고, 실패하면 초기화한 단계만 역순으로 종료한다.
        _startup.add( make_unique<EditorDataSubsystem>( *this ) );
        _startup.add( make_unique<ImGuiContextSubsystem>( *this ) );
        _startup.add( make_unique<PlatformBackendSubsystem>( *this ) );
        _startup.add( make_unique<RendererBackendSubsystem>( *this ) );
        _startup.add( make_unique<EditorContextSubsystem>( *this ) );
        _startup.add( make_unique<WindowCloseQuerySubsystem>( *this ) );
        SubsystemTimingReport::enableTiming( _startup ); // 종료 요약 줄(`ImGuiEditor.ShutdownMs`) — 백엔드 교체 · 리로드의 에디터 몫
        if ( _startup.initializeAll( EngineSubsystemHost::kClient ) == false )
        {
            SW_LOG_ERROR_ID( "ImGuiEditor.EditorStartupFailed", "Editor startup failed: %#", _startup.getError().c_str() );
            _startup.destroyAll();
            return false;
        }

        _bInitialized = SW_TRUE;
        return true;
    }

    SubsystemInitResult ImGuiEditor::initializeEditorData()
    {
        EditorPaths::prepareAutomationStateDirectory(); // 상태 파일을 읽는 어떤 일보다 먼저
        // 사용자 환경설정은 사용자 파일이다 — 처음 실행이면 없고, 깨졌으면 옆으로 치우고 새로 쓴다(`EditorPreferencesStore::loadAll`). 닫을 때까지 저장이 이 파일에 간다.
        if ( EditorPreferencesStore::open() == EditorUserFileResult::Missing )
            SW_LOG_INFO_ID( "ImGuiEditor.EditorPreferencesFirstRun", "Editor preferences: first run - nothing changed from the defaults yet" );
        // 도구 시드는 에디터 필수 데이터다. 대신 쓸 값이 없으므로 읽지 못하면 기동을 멈춘다(이유는 로드가 키 이름과 남겼다).
        _editorToolDefaults = make_unique<EditorToolDefaults>();
        if ( _editorToolDefaults->loadFromHost() == false )
            return SubsystemInitResult::Failed;
        // 아이콘 표 둘도 에디터 필수 데이터다 — 글꼴(아이콘 글리프 범위)이 이 단계 뒤에 서고, 그리기가 이름으로 찾는다. 못 읽으면 이유를 남겼고 멈춘다.
        _pIconGlyphTable     = make_unique<EditorIconGlyphTable>();
        _pComponentIconTable = make_unique<EditorComponentIconTable>();
        if ( _pIconGlyphTable->loadFromAsset( AssetID{ EditorPaths::kIconGlyphTable } ) == false ||
             _pComponentIconTable->loadFromAsset( AssetID{ EditorPaths::kComponentIconTable } ) == false )
            return SubsystemInitResult::Failed;
        // 정적 칸에 걸지 않는다 — 범위가 서기 전의 단계(글꼴)는 인자로 받고, 에디터 컨텍스트가 자기 범위에 빌려 건다(`initializeEditorContext`).
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownEditorData()
    {
        EditorAutosaveCommands::endSession(); // 정상 종료 — 자동 저장 사본과 세션 잠금을 지운다(남으면 다음 기동이 비정상 종료로 보고 복구를 묻는다)
        EditorPreferencesStore::close();      // 서비스가 서 있을 때 마지막으로 쓴다 — 뒤에 모듈이 내려가며 부르는 저장은 쓰지 않는다
        _pComponentIconTable.reset();
        _pIconGlyphTable.reset();
        _editorToolDefaults.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeImGuiContext()
    {
        SW_LOG_TRACE_ID( "ImGuiEditor.CheckingImGuiVersionCreating", "Checking ImGui version and creating context" );
        IMGUI_CHECKVERSION();
        // 컨텍스트보다 먼저 등록한다. 그래야 이 모듈의 ImGui 할당과 해제가 모두 같은 할당자를 거친다. 컨텍스트는 shutdown 이 이 모듈 안에서 지운다.
        ImGui::SetAllocatorFunctions( &ImGuiEditorInternal::allocateForImGui, &ImGuiEditorInternal::freeForImGui, nullptr );
        ImGui::CreateContext();
        ImPlot::CreateContext();
        // 확장 DLL 의 ImGui 사본에 이 컨텍스트를 바인딩한다. 확장마다 CMake 가 만든 바인더를 쓴다. EditorModule 자신은 여기서 CreateContext 를 호출했다.
        EditorUIContextState::publish( EditorUIContextState{ ImGui::GetCurrentContext(), ImPlot::GetCurrentContext() } );

        SW_LOG_TRACE_ID( "ImGuiEditor.ConfiguringImGuiIO", "Configuring ImGui IO" );
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // 멀티 뷰포트는 네 백엔드 모두에 렌더러 백엔드가 있으므로 항상 켠다. 렌더러 백엔드가 없는 백엔드라면 아래
        // createRendererBackend 가 nullptr 을 반환하고 초기화가 거기서 멈춘다.
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // 자체 테스트는 저장된 레이아웃을 읽지도 쓰지도 않는다. 도킹은 자체 테스트를 모르므로 셸이 알려 준다.
        _dockLayout.setPanelOverrideRequested( EditorSelfTestRunner::isRequested() );
        _dockLayout.initializePersistencePaths();
        _dockLayout.applyINIFilename();

        // 글꼴은 데이터(editortooldefaults.json 후보 · 테마 글자 크기)에서 온다. 못 찾으면 이유를 남겼고 기동을 멈춘다.
        if ( _fontSetup.apply( *_editorToolDefaults, *_pIconGlyphTable ) == false )
        {
            shutdownImGuiContext();
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownImGuiContext()
    {
        // 글꼴 아틀라스는 컨텍스트가 보관한다. 확장 DLL 의 사본에서 먼저 분리한다.
        EditorUIContextState::publish( EditorUIContextState{} );
        if ( ImPlot::GetCurrentContext() != nullptr )
            ImPlot::DestroyContext();
        if ( ImGui::GetCurrentContext() != nullptr )
            ImGui::DestroyContext();
    }

    SubsystemInitResult ImGuiEditor::initializePlatformBackend()
    {
        SW_LOG_TRACE_ID( "ImGuiEditor.CreatingPlatformBackend", "Creating Platform Backend" );
        if ( _pWindow == nullptr )
        {
            SW_LOG_ERROR_ID( "ImGuiEditor.FailedCreatePlatformBackend", "Failed to create platform backend: the editor has no window" );
            return SubsystemInitResult::Failed;
        }
        _platformBackend = IImGuiPlatformBackend::createPlatformBackendFor( *_pWindow );
        if ( _platformBackend == nullptr )
        {
            SW_LOG_ERROR_ID( "ImGuiEditor.FailedCreatePlatformBackend2", "Failed to create platform backend" );
            return SubsystemInitResult::Failed;
        }

        SW_LOG_TRACE_ID( "ImGuiEditor.InitializingPlatformBackend", "Initializing Platform Backend" );
        if ( _platformBackend->initialize( _pWindow, _pRHIDevice->getBackendType() ) == false )
        {
            SW_LOG_ERROR_ID( "ImGuiEditor.PlatformBackendInitializationFailed", "Platform backend initialization failed" );
            shutdownPlatformBackend();
            return SubsystemInitResult::Failed;
        }
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        if ( platformIO.Platform_ShowWindow != nullptr )
        {
            ImGuiEditorInternal::s_pfnShowPlatformWindow = platformIO.Platform_ShowWindow;
            ImGuiEditorInternal::s_pMainWindow           = _pWindow;
            platformIO.Platform_ShowWindow               = &ImGuiEditorInternal::showPlatformWindow;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownPlatformBackend()
    {
        if ( _platformBackend == nullptr )
            return;
        ImGuiEditorInternal::s_pMainWindow = nullptr;
        _platformBackend->shutdown();
        _platformBackend.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeRendererBackend()
    {
        SW_LOG_TRACE_ID( "ImGuiEditor.CreatingRendererBackend", "Creating Renderer Backend" );
        // 네 RHI 백엔드가 같은 렌더러 하나를 쓴다(ImGuiRenderer — RHI 로 그린다).
        _renderer = make_unique<ImGuiRenderer>();
        if ( _renderer->initialize( _pRHIDevice, _drawSnapshotHandoff ) == false )
        {
            SW_LOG_ERROR_ID( "ImGuiEditor.RendererBackendInitializationFailed", "Renderer backend initialization failed" );
            shutdownRendererBackend();
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownRendererBackend()
    {
        if ( _renderer == nullptr )
            return;
        _renderer->shutdown();
        _renderer.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeEditorContext()
    {
        _editorContext = make_unique<EditorContext>();
        _editorContext->initialize( *_editorToolDefaults, *_pIconGlyphTable, *_pComponentIconTable, &EditorSubsystemSet::install );
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
        _editorContext->getSubsystem<EditorRenderViews>().setRenderer( _renderer.get() );

        _editorContext->getSubsystem<EditorPanelManager>().registerDefaultPanels();
        EditorCommandGUI::registerDefaults();
        // 리로드 전에 기록한 오브젝트 편집을 되돌려도 이 모듈이 선택과 씬 dirty 상태를 맞춘다.
        _editorContext->getSubsystem<EditorObjectEditBindings>().ensureBound( editor::getSubsystem<CommandStack>() ); // 필수 - 기동 경로다
        EditorExtensionRegistryDump::dumpIfRequested();
        _dockLayout.loadPanelVisibility();
        const bool bResumedSession = ImGuiEditorInternal::resumeEditSession( *_editorContext );

        // 모니터 DPI 에 맞춰 스타일과 글자를 키운다. 테마를 읽은 뒤에 해야 한다. 테마가 96 DPI 기준 크기를 기록하기 때문이다.
        // 모니터를 옮기면 글자와 플랫폼 창이 따라간다(ImGui 1.92 동적 폰트).
        // 배율을 직접 정했으면(`gv_editorUiScale`) 모니터를 옮겨도 글자 배율을 덮어쓰지 않는다.
        // 우선순위는 스위치(자동화), 환경설정(General 의 UI 배율), 모니터 DPI 순이다. 배율이 0 이면 다음 순위로 넘어간다.
        const float32 fixedUIScale  = gv_editorUiScale > 0.0f ? static_cast<float32>( gv_editorUiScale ) : getPreferences<EditorGeneralPreferences>()._uiScale;
        const bool    bFixedUIScale = fixedUIScale > 0.0f;
        EditorThemeUtil::setDPIScale( bFixedUIScale ? fixedUIScale : _platformBackend->getDPIScale() );
        ImGui::GetIO().ConfigDpiScaleFonts     = bFixedUIScale == false;
        ImGui::GetIO().ConfigDpiScaleViewports = bFixedUIScale == false;
        SW_LOG_INFO_ID( "ImGuiEditor.EditorUIScaleFrame", "Editor UI scale %# (%#, frame padding %#x%#)", EditorThemeUtil::getDPIScale(), bFixedUIScale ? "fixed" : "monitor DPI",
                        ImGui::GetStyle().FramePadding.x, ImGui::GetStyle().FramePadding.y );

        // `-gv_editorStartupScene=<경로>` 는 검증용이다. 기동 검증이 오브젝트를 순회하는 코드까지 실행하게 한다. 정의는 이 파일 위쪽에 있다.
        // 스위치가 있으면 그 값을 쓰고, 없으면 환경설정(General 의 시작 씬)을 쓴다.
        // 앞선 인스턴스를 이어받았으면(백엔드 교체 · 에디터 모듈 리로드) 시작 씬을 열지 않는다 — 열면 편집 중인 씬과 미저장 편집을 버린다.
        const string startupScene = gv_editorStartupScene.empty() == false ? string( gv_editorStartupScene ) : getPreferences<EditorGeneralPreferences>()._startupScene;
        if ( startupScene.empty() == false && bResumedSession == false )
        {
            SW_LOG_INFO_ID( "ImGuiEditor.OpeningStartupScene", "Opening the startup scene: %#", startupScene.c_str() );
            if ( EditorSceneFileCommands::loadScene( startupScene ) == false )
                SW_LOG_ERROR_ID( "ImGuiEditor.FailedOpenStartupScene", "Failed to open the startup scene: %#", startupScene.c_str() );
        }
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownEditorContext()
    {
        // 되돌리기 기록 · 복사 버퍼는 컨텍스트의 서브시스템이 내려가며 맡긴다(`EditorUndoHistory` · `ObjectCopyBuffer`). 선택 · 미저장 표시는 여기서.
        if ( _editorContext == nullptr )
            return;
        ImGuiEditorInternal::storeEditSession( *_editorContext );
        // 렌더 스레드는 이미 쉬고 있다(종료 경로가 렌더 워커를 비운 뒤다).
        _pUICapture->releaseRHI( _pRHIDevice );
        _pUICapture = nullptr;
        _editorContext->getSubsystem<EditorRenderViews>().destroyViewTargets();
        _editorContext->getSubsystem<EditorPanelManager>().shutdownAllPanels();
        _editorContext->getSubsystem<EditorPanelManager>().clear();
        _editorContext->shutdown();
        _editorContext.reset();
    }

    SubsystemInitResult ImGuiEditor::initializeWindowCloseQuery()
    {
        // 닫기 확인 칸의 구독은 기동 단계 객체(`WindowCloseQuerySubsystem`)가 쥔다 — 이 셸에 더 할 일이 없다.
        return SubsystemInitResult::Succeeded;
    }

    void ImGuiEditor::shutdownWindowCloseQuery()
    {
        // 선택 조회(②): 종료 경로 - 창이 이미 닫혔으면 되돌릴 제목도 없다.
        MainWindowSubsystem* pWindowHost   = editor::findSubsystem<MainWindowSubsystem>();
        IWindow*             pActiveWindow = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
        if ( pActiveWindow == nullptr )
            return;
        // 에디터를 종료하면 창 제목은 팩 설정 그대로 돌아간다. 창 제목의 출처는 팩 설정(`<Engine _windowTitle>`) 하나뿐이다.
        // 평소 제목만 돌린다 — 창에 거는 일은 엔진이 다음 프레임 끝에 한다(창 제목을 거는 길은 하나).
        pWindowHost->setBaseTitle( editor::getSubsystem<const PackEngineSettings>()._windowTitle ); // 창이 살아 있으면 팩 설정도 있다
    }

    void ImGuiEditor::shutdown()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // 열려 있는 파일 대화상자의 결과 델리게이트를 해제한다. 모듈을 언로드하기 전에 제거해야 하는 이 DLL 안의 주소다.
        // 전역 변수는 모듈을 언로드하는 쪽이 모듈 이름으로 정리한다. 그 델리게이트는 이 DLL 안의 함수와 `this` 를 참조하고,
        // 네이티브 대화상자는 사용자가 닫을 때까지 떠 있다. Undo 스택과 같은 종류의 함정이다.
        FileDialog::cancelResults();

        // 기다리지 않는다. 여기로 오는 경로(GameModuleManager::suspendModules)는 이미 flushRenderWorkers 로 렌더 워커를 비운 뒤이므로
        // 기다릴 대상이 없다.
        abandonPendingDraw();
        for ( EditorDrawDataSnapshot& snapshot : _arrDrawSnapshot )
        {
            snapshot.clear();
        }

        _dockLayout.save();

        // 기동 단계를 초기화의 역순으로 종료하고 목록을 비운다(다시 initialize 할 수 있다).
        _startup.shutdownAll();
        SW_LOG_INFO_ID( "ImGuiEditor.ShutdownMs", "Editor shutdown stages (ms): %#", SubsystemTimingReport::describe( _startup.getShutdownTimings() ).c_str() );
        _startup.destroyAll();
        _pWindow      = nullptr;
        _pRHIDevice   = nullptr;
        _bInitialized = SW_FALSE;
    }

    void ImGuiEditor::prepareFrameState()
    {
        if ( _editorContext == nullptr )
            return;
        // 프레임 앞의 상태 정리는 단계로 옮기지 않는다. 메뉴 · 패널이 이번 프레임에 읽을 값이라 프레임을 열기 전이어야 한다.
        // 확장 모듈은 에디터가 뜬 뒤에도 로드되고 언로드된다. 등록 세대가 바뀌었으면 인스턴스를 맞추고, 같으면 비교 한 번으로 끝난다.
        EditorSubsystemSet::syncExtensionRegistrations( *_editorContext );
        // 토큰 파일이 바뀌었으면(핫 리로드) 프레임을 열기 전에 다시 읽어 이번 프레임부터 새 스타일로 그린다.
        if ( _editorContext->getSubsystem<EditorTheme>().consumeTokensChanged() )
            EditorThemeUtil::reloadThemes();
        // 기동 뒤에 올라온 확장 모듈의 환경설정 섹션은 기본값이다. 새 섹션이 없으면 목록을 한 번 훑고 끝난다.
        if ( EditorPreferencesStore::hasNewSections() )
            (void)EditorPreferencesStore::loadNewSections( EditorPreferencesStore::getDefaultFilePath() ); // 입힌 수는 쓰지 않는다
        _editorContext->getSubsystem<EditorRenderViews>().beginFrameFocus();
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
        // 이 프레임이 만들거나 고친 ImGui 텍스처(글꼴 아틀라스)를 연산으로 옮긴다 — 렌더 스레드는 살아 있는 텍스처 목록을 읽지 않는다. 그 ID 를 그리는 스냅샷보다 먼저 낸다.
        if ( _renderer != nullptr )
            _renderer->queueTextureUpdates();

        // EndFrame 이후에 호출해야 창의 DrawList 에 이번 프레임의 최종 내용이 담긴다.
        // -gv_editorPanelDump=N 이 없으면 아무것도 하지 않는다.
        EditorPanelDump::dumpIfRequested();

        // 텍스처 갱신과 그리기(메인 · 캡처 · 보조 창)는 렌더 스레드가 present 훅에서 한다(`render`). 여기서는 OS 창만 갱신한다 — 보조 창의 스왑체인 생성 · 크기 · 파괴는
        // ImGui 가 이 안에서 렌더러 훅으로 부르고, 디바이스가 렌더 스레드와 GPU 를 비운 뒤에 한다(`ImGuiRenderer::createWindowSwapChain`).
        ImGuiIO& io = ImGui::GetIO();
        if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
        {
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.platformWindows" );
            ImGui::UpdatePlatformWindows();
        }

        // 렌더 스레드가 그리는 중인 칸에는 쓰지 않는다 — 이 UI 프레임은 렌더 스레드가 앞 스냅샷을 그리는 동안 만들어졌다(`DrawSnapshotHandoff`).
        uint32 writeSlot{ 0 };
        {
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.waitDrawSnapshot" );
            writeSlot = _drawSnapshotHandoff.waitForWriteSlot();
        }

        // 스냅샷을 내기 전에 번호를 알린다. 이 뒤에 해제하는 자원은 이 스냅샷이 그릴 수 있으므로 다음 번호를 받아야 한다.
        ++_lastDrawSnapshotSequence;
        {
            SW_EDITOR_PROFILE_SCOPE( "GT.Editor.captureDrawSnapshot" );
            // 에디터 UI 캡처 요청이 있으면 이 프레임의 모든 뷰포트 합본도 넘긴다. 잘림 지표는 창 상태가 남아 있는 지금 측정한다.
            EditorUICaptureJob captureJob{};
            const bool         bCapture = _pUICapture != nullptr && _pUICapture->takeJob( captureJob );
            _arrDrawSnapshot[writeSlot].capture( _lastDrawSnapshotSequence, bCapture ? &captureJob : nullptr );
        }
        if ( _renderer != nullptr )
            _renderer->getDrawReleaseQueue().markSnapshotPublished( _lastDrawSnapshotSequence );
        // 앞에 낸 스냅샷을 렌더 스레드가 아직 집지 않았으면 집을 때까지 기다린다 — 낸 스냅샷을 건너뛰지 않는다(UI 캡처 요청이 실려 있다).
        SW_EDITOR_PROFILE_SCOPE( "GT.Editor.publishDrawSnapshot" );
        _drawSnapshotHandoff.publish( writeSlot );
    }

    void ImGuiEditor::updateUI()
    {
        if ( _bInitialized == SW_FALSE )
            return;

        // 하위 구간은 `GT.Editor.updateUI`(App) 의 시간이 어디에 쓰이는지 나눠 보여 준다. 패널별 시간은 `-gv_editorPanelTimes=N` 으로 본다.
        // 앞 UI 프레임이 다 그려지기를 기다리지 않는다 — 렌더 스레드가 그것을 그리는 동안 이 프레임을 만든다. 기다림은 쓸 칸 받기 · 게시에만 있다(`closeAndPublishFrame`).
        // 그 구간(`GT.Editor.waitDrawSnapshot` · `GT.Editor.publishDrawSnapshot`)이 크면 UI 작업이 아니라 RT, GPU, Present 가 길다는 뜻이다.
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

    void ImGuiEditor::prepareRender( IRHIDevice* pRHIDevice )
    {
        _renderSlot = DrawSnapshotHandoff::kInvalidSlot;
        if ( _bInitialized == SW_FALSE || pRHIDevice == nullptr || _renderer == nullptr )
            return;

        // 렌더 스레드가 백버퍼 패스를 열기 직전이다 — 업로드(그리기 목록 버퍼 · 텍스처 연산 · bindless 등록)는 여기서 한다. 열린 패스 안에서 하면 Vulkan 은 복사를 위해
        // 패스를 닫고, 그 뒤의 UI 드로우가 버려졌다. UI 스레드는 그동안 다음 UI 프레임을 만든다 — 여기서는 살아 있는 ImGui 상태를 읽지 않고 스냅샷과 텍스처 연산만 읽는다.
        const uint32 slot = _drawSnapshotHandoff.beginRender();
        if ( slot >= constant::render::kMaxFrameCountInFlight )
            return;
        _renderSlot = slot;

        // 이 프레임에 그릴 목록(메인 · 캡처 · 보조 창)을 버퍼 하나에 한 번 올린다.
        EditorDrawDataSnapshot& snapshot = _arrDrawSnapshot[slot];
        _listFrameDrawData.clear();
        _listFrameDrawData.push_back( snapshot.getMainDrawData() );
        _listFrameDrawData.push_back( snapshot.findCaptureDrawData() );
        for ( uint32 windowIndex = 0; windowIndex < snapshot.getPlatformWindowCount(); ++windowIndex )
        {
            _listFrameDrawData.push_back( snapshot.getPlatformWindowDrawData( windowIndex ) );
        }
        _renderer->prepareFrame( *pRHIDevice, _listFrameDrawData );
    }

    void ImGuiEditor::render( IRHIDevice* pRHIDevice )
    {
        // 열린 백버퍼 패스 안이다. 그리기만 한다 — 업로드는 `prepareRender` 가 했다.
        if ( _bInitialized == SW_FALSE || pRHIDevice == nullptr || _renderer == nullptr || _renderSlot == DrawSnapshotHandoff::kInvalidSlot )
            return;
        EditorDrawDataSnapshot& snapshot         = _arrDrawSnapshot[_renderSlot];
        ImDrawData* const       pDrawData        = snapshot.getMainDrawData();
        ImDrawData* const       pCaptureDrawData = snapshot.findCaptureDrawData();
        if ( pDrawData != nullptr )
            renderBackend( pRHIDevice, pDrawData );
        // 에디터 UI 캡처: 같은 프레임의 모든 뷰포트 합본을 캡처 텍스처에 한 번 더 그린다. `renderJob` 은 자기가 연 패스를 닫고 돌아온다.
        if ( pCaptureDrawData != nullptr && _pUICapture != nullptr )
            _pUICapture->renderJob( *pRHIDevice, *_renderer, *pCaptureDrawData, snapshot.getCaptureJob() );
        // 보조(플로팅) 창 — 창마다 그 창의 스왑체인 백버퍼에 그린다. 표시는 이 프레임의 endFrame 이 한다.
        _renderer->renderPlatformWindows( *pRHIDevice, snapshot );

        // 이 프레임의 스냅샷보다 먼저 해제 요청된 자원은 앞 프레임들만 그렸다. 이 프레임의 GPU 작업이 끝난 뒤에 해제되도록 디바이스로 넘긴다.
        _renderer->getDrawReleaseQueue().handOverToDevice( *pRHIDevice, snapshot.getSequence() );
    }

    void ImGuiEditor::postPresent( IRHIDevice* pRHIDevice )
    {
        // endFrame 뒤라 이 프레임에 그린 캡처를 읽을 수 있다(GPU 를 기다린다 — 캡처한 프레임만).
        if ( _pUICapture != nullptr && pRHIDevice != nullptr )
            _pUICapture->finishJob( *pRHIDevice );
        // 메인 스냅샷 렌더가 끝났으므로 UI 스레드가 다음 슬롯을 쓸 수 있게 해제한다.
        _renderSlot = DrawSnapshotHandoff::kInvalidSlot;
        // 보조 뷰포트는 updateUI 에서 UI 스레드가 이미 렌더하고 present 했다.
        _drawSnapshotHandoff.endRender();
    }

    void ImGuiEditor::abandonPendingDraw()
    {
        // in-flight 표시는 "렌더 스레드가 이 슬롯을 읽고 postPresent 에서 해제한다" 는 약속이다. 에디터 인스턴스를 없애면(destroyEditorInstance)
        // 그 약속을 지킬 쪽이 사라지므로 여기서 푼다. 풀지 않으면 종료의 보조 창 정리가 낸 스냅샷을 기다리며(`ImGuiRenderer::waitDrawnBeforeWindowChange`) 반환하지 않는다.
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
            const ScopedImGuiOSInputFilter osInputFilter{ ImGuiEditorInternal::isOSDeviceInputBlocked() };
            _platformBackend->processEvent( event );
        }

        // ImGui 가 가져간 입력은 게임으로 넘기지 않는다. 게임 뷰 위에서는 예외다. 씬 뷰 위의 입력은 에디터 카메라가 처리한다.
        // 에디터 창이 가진 키는 OS 메시지 대신 ImGui 가 받은 키로 엔진 입력 매니저에 들어간다(`EditorInputActions::forwardEditorKeyboard`) — 주 창 · 떼어 낸 창 ·
        // 자동화가 넣은 키가 한 길로 에디터 입력 맵에 닿고, 그동안 게임 · 셸 · UI 맵은 키보드 포커스 `Tool` 로 가려진다.
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

    void ImGuiEditor::getGameViewport( ViewportTarget* pOutTarget ) const
    {
        const bool bRequest = _editorContext != nullptr && _editorContext->getSubsystem<EditorRenderViews>().isViewRequested( EditorViewKind::Game );
        ImGuiEditorInternal::writeViewport( bRequest ? &_editorContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Game ) : nullptr, pOutTarget );
    }

    void ImGuiEditor::getSceneViewport( ViewportTarget* pOutTarget ) const
    {
        const bool bRequest = _editorContext != nullptr && _editorContext->getSubsystem<EditorRenderViews>().isViewRequested( EditorViewKind::Scene );
        ImGuiEditorInternal::writeViewport( bRequest ? &_editorContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Scene ) : nullptr, pOutTarget );
    }

    uint32 ImGuiEditor::getPreviewSceneViews( PreviewSceneView* pOutView, uint32 capacity ) const
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

    void ImGuiEditor::requestBackgroundStop()
    {
        requestEditorBackgroundStop();
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

        if ( _platformBackend != nullptr )
        {
            // 무인 자동화 중이면 플랫폼이 조회한 실제 커서를 지운다. Win32 는 GetCursorPos 와 커서 아래 뷰포트, X11 은 XQueryPointer 의 위치와 버튼이 해당한다.
            // 테스트가 커서를 놓은 프레임(버튼을 놓는 프레임)에 실제 커서가 마지막 위치가 되면 ImGui 가 버튼 놓기를 다음 프레임으로 미뤄 뷰포트 비행이 그 차이만큼 돈다.
            const ScopedImGuiOSInputFilter osInputFilter{ ImGuiEditorInternal::isOSDeviceInputBlocked() };
            _platformBackend->newFrame();
        }
        // 자체 테스트가 만든 가상 입력은 플랫폼 백엔드가 넣은 실제 커서보다 뒤에 넣어야 우선한다. ImGui 는 큐를 순서대로 처리하기 때문이다.
        EditorSelfTestInput::flushIntoImGui();

        // 이름 붙인 레이아웃은 프레임 밖에서 읽어야 이미 있는 창과 도킹 노드에 적용된다.
        _dockLayout.applyPendingNamedLayout();
        // 플랫폼 백엔드는 io.DeltaTime 에 벽시계 경과를 적는다. 패널이 각자 우회하지 않도록 ImGui 경과를 엔진 경과로 맞춘다.
        // 자동화에서는 엔진 경과가 고정 프레임 시간이라 녹화와 재생이 같은 시간을 읽는다. 벽시계가 필요한 프로파일러 성능 탭은 자기 시계를 읽는다.
        ImGui::GetIO().DeltaTime = MathUtil::max( editor::getSubsystem<GameTimeScale>().getUnscaledDeltaTime(), ImGuiEditorInternal::kMinDeltaTime );
        ImGui::NewFrame();
        // 모니터를 옮기면 ImGui 가 글자 배율(FontScaleDpi)만 새 DPI 로 덮어쓴다(ConfigDpiScaleFonts). 테마의 여백과 둥글기는 이전 배율이 남으므로 따라가게 한다.
        // 그러지 않으면 150 % 모니터에서 글자만 커지고 위젯 크기는 그대로다. 배율을 직접 정했으면(gv_editorUiScale) ImGui 가 덮어쓰지 않으므로 여기서도 건드리지 않는다.
        // 이 프레임의 위젯은 아직 그리지 않았다. 스타일 크기는 그릴 때 읽힌다.
        {
            const float32 fontScaleDPI = ImGui::GetStyle().FontScaleDpi;
            const bool    bDPIMoved    = ImGui::GetIO().ConfigDpiScaleFonts && fontScaleDPI > 0.0f &&
                                   MathUtil::abs( fontScaleDPI - EditorThemeUtil::getDPIScale() ) > ImGuiEditorInternal::kDPIFollowTolerance;
            if ( bDPIMoved )
                EditorThemeUtil::setDPIScale( fontScaleDPI );
        }
        // 실행 중에 `gv_editorUiScale` 을 바꾸면(콘솔, 시나리오 `<Variable>`, gv 패널) 바로 반영한다. 0 으로 되돌리면 다시 모니터 DPI 를 따른다.
        {
            const bool    bFixedUIScale = gv_editorUiScale > 0.0f;
            const float32 wantedScale   = bFixedUIScale ? static_cast<float32>( gv_editorUiScale ) : _platformBackend != nullptr ? _platformBackend->getDPIScale()
                                                                                                                                 : 1.0f;
            const bool    bModeChanged  = ImGui::GetIO().ConfigDpiScaleFonts == bFixedUIScale;
            const bool    bFixedMoved   = bFixedUIScale && MathUtil::abs( wantedScale - EditorThemeUtil::getDPIScale() ) > ImGuiEditorInternal::kDPIFollowTolerance;
            if ( bModeChanged || bFixedMoved )
            {
                ImGui::GetIO().ConfigDpiScaleFonts     = bFixedUIScale == false;
                ImGui::GetIO().ConfigDpiScaleViewports = bFixedUIScale == false;
                EditorThemeUtil::setDPIScale( wantedScale );
                SW_LOG_INFO_ID( "ImGuiEditor.EditorUIScale", "Editor UI scale %# (%#)", EditorThemeUtil::getDPIScale(), bFixedUIScale ? "fixed" : "monitor DPI" );
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

        if ( _platformBackend != nullptr )
            _platformBackend->drawOverlay();
        ImGui::Render();
    }

    void ImGuiEditor::renderBackend( IRHIDevice* pRHIDevice, ImDrawData* pDrawData )
    {
        if ( _bInitialized == SW_FALSE )
            return;

        if ( _renderer != nullptr && pRHIDevice != nullptr && pDrawData != nullptr )
            _renderer->render( *pRHIDevice, *pDrawData );
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorFrameStepRegistration )
} // namespace sw::editor

// ==============================================================================
// EditorModule C-ABI 진입점(매크로가 구현한다)
// ==============================================================================
SW_IMPLEMENT_EDITOR_MODULE( sw::editor::ImGuiEditor );
