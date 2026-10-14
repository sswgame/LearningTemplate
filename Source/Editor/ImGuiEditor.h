/**
 * @file ImGuiEditor.h
 * @brief ImGui 에디터 호스트입니다. OS 와 GPU 백엔드, 프레임 루프를 담당합니다.
 */
#pragma once
#include "Core/Subsystem/SubsystemCollection.h"

#include "Editor/Common/Backend/DrawSnapshotHandoff.h"
#include "Editor/Common/Backend/EditorDrawDataSnapshot.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/IEditor.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    struct EditorToolDefaults;

    class EditorContext;
    class IImGuiPlatformBackend;
    class IImGuiRendererBackend;

    /** @brief ImGui 플랫폼과 렌더러의 호스트입니다. 메뉴, 도크, 폰트는 Common 에 맡깁니다. */
    class ImGuiEditor : public IEditor
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 주기. 생성자는 플래그만 설정하고, GPU 와 ImGui 는 initialize 와 shutdown 에서 다룬다
        // ------------------------------------------------------------------------------
        /** @brief ImGui 에디터 셸을 만듭니다. */
        ImGuiEditor();
        /** @brief ImGui 에디터 셸을 파괴합니다. */
        virtual ~ImGuiEditor() override;

        // ------------------------------------------------------------------------------
        // 2) IEditor: 초기화, 프레임, 이벤트, 뷰, 플레이 상태
        // ------------------------------------------------------------------------------
        /** @brief 플랫폼 백엔드, 렌더러, 폰트를 초기화합니다. */
        bool initialize( IWindow* pWindow, IRHIDevice* pRHIDevice ) override;
        /** @brief 에디터 리소스를 해제합니다. */
        void shutdown() override;
        /** @brief 메인 스레드에서 ImGui 프레임을 갱신하고, 패널을 그리고, 플랫폼 창을 갱신합니다. */
        void updateUI() override;
        /** @brief UI 를 그리기 전에 패널의 GPU 작업을 합니다. */
        void preRender( IRHIDevice* pRHIDevice ) override;
        /** @brief 에디터 UI 의 DrawData 를 GPU 로 그립니다. */
        void render( IRHIDevice* pRHIDevice ) override;
        /** @brief 메인 스왑체인 Present 뒤에 멀티 뷰포트를 그립니다. */
        void postPresent( IRHIDevice* pRHIDevice ) override;
        /** @brief 렌더 대기 중인 draw 스냅샷 표시를 버립니다. 에디터 인스턴스를 없앨 때만 부릅니다(`DrawSnapshotHandoff::abandon`). */
        void abandonPendingDraw() override;
        /** @brief 네이티브 이벤트를 ImGui 플랫폼 레이어에 전달합니다. */
        bool processEvent( const NativeWindowEvent& event ) override;
        /** @brief 이번 프레임 게임 뷰의 렌더 타깃 핸들과 크기를 조회합니다. */
        void getGameViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const override;
        /** @brief 이번 프레임 씬 뷰의 렌더 타깃 핸들과 크기를 조회합니다. */
        void getSceneViewport( uint64* pRenderTarget, uint32* pWidth, uint32* pHeight ) const override;
        /** @brief 이번 프레임 씬 뷰를 그리는 카메라를 반환합니다. */
        CameraComponent* getSceneViewCamera() const override;
        /** @brief 이번 프레임에 보인 미리보기 씬 뷰(툴 창)를 씁니다(`EditorRenderViews::collectSceneViews`). */
        uint32 getSceneViews( HostSceneView* pOutView, uint32 capacity ) const override;
        /** @brief 에디터 시뮬레이션(PIE)이 실행 중이면 true 입니다. */
        bool isPlaying() const override;
        /** @brief 에디터 시뮬레이션이 일시정지 상태이면 true 입니다. */
        bool isPaused() const override;
        /** @brief 에디터 시뮬레이션(PIE)을 정지합니다. */
        void stopSimulation() override;
        /** @brief 월드 틱 뒤에 Step 을 소비합니다. */
        void onHostFrameEnd() override;

    private:
        // ------------------------------------------------------------------------------
        // 기동 단계. 단계마다 작은 서브시스템 클래스(아래 중첩 클래스 — 정의는 `ImGuiEditor.cpp`)가 이 함수들을 부르고, 순서는 단계마다 선언한 의존이 정한다.
        // 실패하면 목록이 초기화에 성공한 단계만 역순으로 종료한다. 초기화가 실패하면 그 단계가 만든 것은 그 자리에서 놓는다.
        // ------------------------------------------------------------------------------
        /** @brief 환경설정과 에디터 데이터(`EditorToolDefaults`)를 읽습니다. 단계 `EditorData`. */
        SubsystemInitResult initializeEditorData();
        void                shutdownEditorData();
        /** @brief ImGui · ImPlot 컨텍스트, IO, 도킹 저장 경로, 글꼴을 세웁니다. 단계 `ImGuiContext`(글꼴이 에디터 데이터를 읽는다). */
        SubsystemInitResult initializeImGuiContext();
        void                shutdownImGuiContext();
        /** @brief 플랫폼 백엔드. 단계 `PlatformBackend`. */
        SubsystemInitResult initializePlatformBackend();
        void                shutdownPlatformBackend();
        /** @brief 렌더러 백엔드(플랫폼 백엔드 뒤). 단계 `RendererBackend`. */
        SubsystemInitResult initializeRendererBackend();
        void                shutdownRendererBackend();
        /** @brief 기본 렌더 패스 · 포워드 파이프라인을 워커에서 읽고 기다립니다. 단계 `Splash`(종료할 것이 없다). */
        SubsystemInitResult loadSplashAssets();
        /** @brief 에디터 컨텍스트, 테마, 기본 패널, 단축키, Undo 편집 리스너, UI 배율, 시작 씬. 단계 `EditorContext`. */
        SubsystemInitResult initializeEditorContext();
        void                shutdownEditorContext();
        /** @brief 창 닫기 질의 처리기(이 DLL 의 메서드를 가리킨다 — 가장 먼저 내린다). 단계 `WindowCloseQuery`. */
        SubsystemInitResult initializeWindowCloseQuery();
        void                shutdownWindowCloseQuery();
        bool                onWindowCloseQuery();

        class EditorContextSubsystem;
        class EditorDataSubsystem;
        class ImGuiContextSubsystem;
        class PlatformBackendSubsystem;
        class RendererBackendSubsystem;
        class SplashSubsystem;
        class WindowCloseQuerySubsystem;
        // ------------------------------------------------------------------------------
        // 3) ImGui 프레임과 백엔드 렌더
        // ------------------------------------------------------------------------------
        /** @brief ImGui 프레임을 시작합니다. */
        void beginFrame();
        /** @brief ImGui 프레임을 종료합니다. */
        void endFrame();
        /** @brief ImGui 렌더러 백엔드로 주어진 DrawData 를 그립니다. */
        void renderBackend( IRHIDevice* pRHIDevice, ImDrawData* pDrawData );
        /** @brief 렌더 스레드가 이전 스냅샷을 쓰는 동안 NewFrame 을 미룹니다. */
        void waitForDrawSnapshotIdle();

    private:
        SubsystemCollection               _startup;    ///< 위의 기동 단계들 — 의존 순서로 초기화하고 역순으로 종료한다
        IWindow*                          _pWindow;    ///< `initialize` 가 받은 창(기동 단계가 읽는다)
        IRHIDevice*                       _pRHIDevice; ///< `initialize` 가 받은 디바이스(기동 단계가 읽는다)
        unique_ptr<IImGuiPlatformBackend> _platformBackend;
        unique_ptr<IImGuiRendererBackend> _rendererBackend;
        unique_ptr<EditorToolDefaults>    _editorToolDefaults;
        unique_ptr<EditorContext>         _editorContext;
        EditorDockLayout                  _dockLayout;
        EditorDrawDataSnapshot            _arrDrawSnapshot[constant::kMaxFrameCountInFlight];
        /// @brief `_arrDrawSnapshot` 슬롯을 UI 스레드와 렌더 스레드가 주고받는 약속입니다. 불변식은 `DrawSnapshotHandoff` 에 있습니다.
        DrawSnapshotHandoff _drawSnapshotHandoff;
        /// @brief 마지막으로 낸 draw 스냅샷 번호입니다. UI 스레드 전용이며 1 부터 시작합니다. 해제한 리소스의 해제 순서를 이 번호로 정합니다(`EditorDrawReleaseQueue`).
        uint64 _lastDrawSnapshotSequence;

        uint8                  _bInitialized  : 1;
        [[maybe_unused]] uint8 _reservedFlags : 7;
    };
} // namespace sw::editor
