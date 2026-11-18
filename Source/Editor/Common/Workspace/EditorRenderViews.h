/**
 * @file EditorRenderViews.h
 * @brief 에디터가 그리는 뷰(씬 · 게임 · 툴 창의 미리보기 씬)의 렌더 타깃과, 그것을 만들고 ImGui 에 등록하는 RHI 디바이스 · 렌더러 백엔드, 게임 뷰의 입력 상태를 가진 에디터 서브시스템입니다.
 * @details 셸(`ImGuiEditor`)이 컨텍스트를 만든 뒤 디바이스와 백엔드를 알리고(`setRHIDevice` · `setRenderer`), 패널이 뷰 크기에 맞춰 렌더 타깃을 만들고
 *          그린 것을 표시하며, 호스트는 프레임마다 핸들만 읽는다(`ImGuiEditor::getSceneViewport` · `getGameViewport` · `getPreviewSceneViews`). 종료(`shutdownSubsystem`)에서 렌더 타깃을 놓는다.
 *          씬 뷰 · 게임 뷰는 활성 씬의 고정 두 슬롯이고, 툴 창의 미리보기 타깃은 id 로 찾는 가변 목록이다(`allocatePreviewTarget`).
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

#include "Engine/Graphics/Texture/TextureCache.h"

namespace sw
{
    class IRHIDevice;
    struct PreviewSceneView;
} // namespace sw

namespace sw::editor
{
    class ImGuiRenderer;

    /**
     * @brief 한 프레임에 받는 미리보기 뷰 수입니다 — 호스트가 받는 수(`kMaxPreviewSceneView`)와 같다(.cpp 의 static_assert).
     * @details 호스트 형식 헤더(`FrameViewports.h`)는 RHI 핸들을 담아 이 헤더를 읽는 하위 대상(에디터 확장 플러그인)이 볼 수 없다 — 그래서 수만 여기 둔다.
     */
    inline constexpr uint32 kMaxEditorPreviewView = 8;

    /** @brief 에디터가 소유하는 뷰 렌더 타깃 하나입니다. 씬 뷰와 게임 뷰가 각각 하나씩 가집니다. 호스트는 프레임마다 핸들만 조회합니다(`ImGuiEditor::getSceneViewport`, `getGameViewport`). */
    struct EditorViewTarget
    {
        // 지금 그리는 타깃 — 엔진 렌더 텍스처(`rendertarget/editor/…`)의 로드 핸들(참조를 쥔다)과 GPU 에 올라간 뒤의 핸들. GPU 자원은 엔진이 렌더 쪽에서 만든다
        TextureHandle _texture{};
        string        _texturePath{};
        uint64        _renderTarget{ 0 }; ///< 0 이면 아직 그릴 타깃이 없다(호스트에 알리지도 그리지도 않는다)
        void*         _pTextureID{ nullptr };
        uint32        _width{ 0 }; ///< 지금 그리는 타깃의 크기
        uint32        _height{ 0 };
        // 크기를 바꾸는 중이면 새 타깃 — 준비될 때까지 지금 타깃을 계속 그리고, 준비되면 바꿔 끼운다(언리얼 `ResizeTarget` 과 같다).
        // 예전에는 옛 타깃을 먼저 버려, 그 사이 미리보기가 패킷에서 빠지고 하위 렌더러가 통째로 내려갔다가 다시 섰다.
        TextureHandle _pendingTexture{};
        string        _pendingPath{};
        uint32        _pendingWidth{ 0 };
        uint32        _pendingHeight{ 0 };

        /** @brief 마지막으로 요청한 크기입니다(바꾸는 중이면 새 크기). 크기 비교는 이것으로 한다. */
        uint32 getRequestedWidth() const { return _pendingTexture.isValid() ? _pendingWidth : _width; }
        uint32 getRequestedHeight() const { return _pendingTexture.isValid() ? _pendingHeight : _height; }
    };

    /**
     * @struct EditorViewTargetState
     * @brief 게임 스레드가 가진 뷰 렌더 타깃 하나를 UI 가 읽는 값입니다 — 그릴 ImGui 텍스처 id, 렌더 타깃 핸들, 지금 크기와 마지막으로 요청한 크기.
     * @details 게임 → UI 방향의 스냅샷이다(`EditorRenderViews::publishTargetStates`). UI 는 이 값만 읽고, 타깃을 만들거나 바꾸려면 명령을 보낸다(`EditorUICommandKind::FitViewTarget` · `FitPreviewTarget`).
     */
    struct EditorViewTargetState
    {
        void*  _pTextureID{ nullptr };
        uint64 _renderTarget{ 0 };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
        uint32 _requestedWidth{ 0 };
        uint32 _requestedHeight{ 0 };

        /** @brief 타깃의 지금 값을 뜹니다. */
        static EditorViewTargetState capture( const EditorViewTarget& target )
        {
            EditorViewTargetState state;
            state._pTextureID      = target._pTextureID;
            state._renderTarget    = target._renderTarget;
            state._width           = target._width;
            state._height          = target._height;
            state._requestedWidth  = target.getRequestedWidth();
            state._requestedHeight = target.getRequestedHeight();
            return state;
        }
    };

    /** @brief 미리보기 타깃 하나의 UI 쪽 값입니다(`EditorViewTargetState` 와 id). */
    struct EditorPreviewTargetState
    {
        EditorViewTargetState _state{};
        uint32                _id{ 0 };
    };

    /** @brief 게임 스레드가 넘기는 뷰 · 미리보기 타깃 값 묶음입니다(게임 → UI). */
    struct EditorTargetSnapshot
    {
        EditorViewTargetState            _arrView[static_cast<uint32>( EditorViewKind::Count )]{};
        vector<EditorPreviewTargetState> _listPreview{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 툴 창(미리보기 씬)이 가지는 뷰 렌더 타깃 하나입니다. id 로 찾습니다(`EditorRenderViews::allocatePreviewTarget`).
     * @details 호스트에는 `FrameViewports::_arrPreviewView` 로 간다 — 보인 프레임(`submitPreviewView` 가 UI 프레임 값 묶음에 적은 눈)에만 간다.
     */
    struct EditorPreviewViewTarget
    {
        EditorViewTarget _target{};
        uint32           _id{ 0 };
    };

    /**
     * @struct EditorUIFrameOutput
     * @brief UI 프레임이 쓰고 게임 스레드가 읽는 값 묶음입니다 — 그린 뷰와 그 자리, 미리보기 뷰의 눈, 게임 뷰 초점 · 마우스 올림, UI 쪽 다시 그릴 까닭.
     * @details 에디터 UI 스레드 분리의 UI → 게임 방향 계약이다(`docs/DesignBacklog.md` "에디터 UI 스레드 분리 검토" 3). UI 는 프레임 동안 자기 묶음에만 쓰고,
     *          프레임 끝(`EditorRenderViews::publishUIFrame`)에 통째로 복사해 넘긴다. 게임 스레드는 넘긴 사본만 읽는다(`getPublishedUIFrame`) — 다음 UI 프레임이 쓰는 중인 값을 보지 않는다.
     *          게임 → UI 방향은 View 가 받는 ViewState 스냅샷이고, UI → 게임의 의도는 명령(`EditorUICommandQueue`)이다. 세 길 모두 값만 건너간다(언리얼 Slate 가 게임 스레드와 주고받는 모양).
     *          포인터 · 핸들 없이 고정 크기 값만 들어 복사가 할당하지 않는다.
     */
    struct EditorUIFrameOutput
    {
        /** @brief 뷰 하나를 이번 UI 프레임에 그렸는지와 그 자리입니다. */
        struct ViewMark
        {
            int32 _windowX{ 0 };       ///< 이미지 왼쪽 위가 놓인 주 창 클라이언트 픽셀(`markViewDrawn`). 호스트가 포인터를 뷰 픽셀로 옮긴다
            int32 _windowY{ 0 };       ///< 위와 같습니다(세로)
            uint8 _bDrawn{ SW_FALSE }; ///< 패널이 이 뷰를 그렸다(보인다)
        };

        /** @brief 툴 창이 이번 UI 프레임에 보인 미리보기 뷰 하나의 눈입니다(`submitPreviewView`). */
        struct PreviewRequest
        {
            float4x4 _viewProj{};
            float3   _cameraPos{};
            uint32   _id{ 0 };       ///< 미리보기 타깃 id(`allocatePreviewTarget`)
            uint32   _sceneKey{ 0 }; ///< 그릴 미리보기 씬(`SceneManager::getPreviewSceneKey`)
        };

        ViewMark       _arrView[static_cast<uint32>( EditorViewKind::Count )]{};
        PreviewRequest _arrPreview[kMaxEditorPreviewView]{};
        uint32         _previewCount{ 0 };
        uint32         _redrawReasons{ 0 };                   ///< UI 쪽 다시 그릴 까닭(`EditorRedrawReason` 비트 — 입력 · 잡은 항목 · 연속 프레임 요청). 게임 쪽 까닭은 읽는 쪽이 더한다
        uint8          _bGameViewFocused{ SW_FALSE };         ///< 게임 뷰 창이 포커스를 가졌다
        uint8          _bGameViewHovered{ SW_FALSE };         ///< 게임 뷰 창 위에 마우스가 있다
        uint8          _bGameInputSurfaceFocused{ SW_FALSE }; ///< 게임 입력을 받는 다른 면(한 프로세스 재생의 월드 뷰포트)이 포커스를 가졌다
        uint8          _bAnimationRequested{ SW_FALSE };      ///< 패널이 연속 프레임을 청했다(`requestAnimationFrame`)
        uint8          _bEditorOwnsKeyboard{ SW_FALSE };      ///< 에디터 창이 키보드를 가졌다 — 게임 스레드가 엔진 키보드 포커스를 `Tool` 로 둔다(`EditorInputActions::applyKeyboardOwner`)

        /** @brief 그 뷰를 그렸으면 true 입니다. */
        bool wasViewDrawn( EditorViewKind kind ) const { return _arrView[static_cast<uint32>( kind )]._bDrawn == SW_TRUE; }
        /** @brief 호스트에 그 뷰의 렌더 타깃을 알릴지입니다(`EditorViewTargetUtil::shouldRequestSceneView` · `shouldRequestGameView`). */
        bool isViewRequested( EditorViewKind kind ) const
        {
            const bool bSceneDrawn = wasViewDrawn( EditorViewKind::Scene );
            const bool bGameDrawn  = wasViewDrawn( EditorViewKind::Game );
            return kind == EditorViewKind::Scene ? EditorViewTargetUtil::shouldRequestSceneView( bSceneDrawn, bGameDrawn ) : EditorViewTargetUtil::shouldRequestGameView( bGameDrawn );
        }
        /** @brief 게임 뷰 또는 게임 입력 면이 포커스였으면 true 입니다. */
        bool isGameInputFocused() const { return _bGameViewFocused == SW_TRUE || _bGameInputSurfaceFocused == SW_TRUE; }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorRenderViews
     * @brief 뷰 렌더 타깃 · RHI 디바이스 · ImGui 렌더러 백엔드 · 게임 뷰 입력 상태입니다(`EditorContext::getSubsystem<EditorRenderViews>()`).
     */
    class SW_EDITOR_API EditorRenderViews final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorRenderViews";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /** @brief 남은 뷰 렌더 타깃을 놓고 디바이스 · 백엔드를 잊습니다. */
        void shutdownSubsystem() override;

        void        setRHIDevice( IRHIDevice* pDevice ) { _pRHIDevice = pDevice; }
        IRHIDevice* getRHIDevice() const { return _pRHIDevice; }
        void        setRenderer( ImGuiRenderer* pBackend ) { _pRenderer = pBackend; }
        /** @brief ImGui 렌더러 백엔드를 반환합니다. 텍스처를 ImGui 에 등록하려는 패널이 씁니다. 없으면 nullptr 입니다. */
        ImGuiRenderer* getRenderer() const { return _pRenderer; }

        // ---- UI 프레임 쪽(UI 스레드만 쓰고 읽는다) — 이번 UI 프레임의 값 묶음(`EditorUIFrameOutput`)에 쓴다. 게임 스레드는 `getPublishedUIFrame` 만 읽는다.

        void setGameViewHovered( bool bHovered ) { _uiFrame._bGameViewHovered = bHovered ? SW_TRUE : SW_FALSE; }
        void setGameViewFocused( bool bFocused ) { _uiFrame._bGameViewFocused = bFocused ? SW_TRUE : SW_FALSE; }
        /** @brief 이번 UI 프레임에 게임 뷰 위에 마우스가 있으면 true 입니다(UI 쪽 — 창 메시지를 번역하는 `ImGuiEditor::processEvent` 도 UI 쪽이다). */
        bool isGameViewHovered() const { return _uiFrame._bGameViewHovered == SW_TRUE; }
        /** @brief 이번 UI 프레임에 게임 뷰에 포커스가 있으면 true 입니다(UI 쪽). */
        bool isGameViewFocused() const { return _uiFrame._bGameViewFocused == SW_TRUE; }
        /**
         * @brief 게임 입력을 받는 다른 면(한 프로세스 재생의 월드 뷰포트 등)이 이번 프레임에 포커스를 가졌음을 알립니다. 패널이 그리는 동안 부릅니다.
         * @details 게임 뷰 패널처럼 그 창이 포커스면 키보드를 도구(에디터)가 아니라 게임 쪽에 둔다(`EditorInputActions::decideKeyboardOwner`).
         */
        void recordGameInputSurfaceFocused() { _uiFrame._bGameInputSurfaceFocused = SW_TRUE; }
        /**
         * @brief 프레임 앞 정리 — 지난 프레임에 패널이 알린 포커스를 "지난 프레임 값" 으로 옮기고 이번 프레임 값을 비웁니다.
         * @details 키보드 주인 판정(`EditorInputActions::processFrame`)은 패널을 그리기 **전에** 돈다. 비운 값을 읽으면 게임 뷰 포커스가 늘 거짓이라
         *          게임이 키를 받지 못한다 — 그래서 판정은 지난 프레임 값(`wasGameInputFocusedLastFrame`)을 읽는다.
         */
        void beginFrameFocus()
        {
            _bGameInputFocusedLastFrame        = _uiFrame.isGameInputFocused() ? SW_TRUE : SW_FALSE;
            _uiFrame._bGameViewFocused         = SW_FALSE;
            _uiFrame._bGameViewHovered         = SW_FALSE;
            _uiFrame._bGameInputSurfaceFocused = SW_FALSE;
        }
        /** @brief UI 쪽 — 이번 UI 프레임에 에디터 창이 키보드를 가졌는지 적습니다(`EditorInputActions::decideKeyboardOwner`). 프레임마다 다시 적으므로 앞 정리가 지우지 않는다. */
        void setEditorOwnsKeyboard( bool bOwns ) { _uiFrame._bEditorOwnsKeyboard = bOwns ? SW_TRUE : SW_FALSE; }
        /** @brief 지난 프레임에 게임 뷰 또는 게임 입력 면이 포커스였는지입니다(키보드 주인 판정용, UI 쪽). */
        bool wasGameInputFocusedLastFrame() const { return _bGameInputFocusedLastFrame == SW_TRUE; }

        /**
         * @brief UI 프레임 끝 — 이번 UI 프레임의 값 묶음에 UI 쪽 다시 그릴 까닭 @p uiRedrawReasons 를 적고 통째로 게임 스레드 쪽 사본으로 넘깁니다.
         * @details 셸이 UI 프레임을 닫은 뒤 한 번 부른다(`ImGuiEditor::updateUI`). 넘긴 뒤 UI 가 쓰는 값은 다음 넘김까지 게임 스레드에 보이지 않는다.
         */
        void publishUIFrame( uint32 uiRedrawReasons );
        /** @brief 게임 스레드 쪽 — 마지막으로 넘긴 UI 프레임 값 묶음의 사본입니다(값으로 돌려준다). */
        [[nodiscard]] EditorUIFrameOutput getPublishedUIFrame() const;

        // ---- 렌더 타깃(게임 스레드가 소유한다) — UI 는 `getUITargetState` · `findUIPreviewTargetState` 의 값만 읽고, 바꾸려면 명령을 보낸다.

        /** @brief 게임 스레드 쪽 — 뷰 RT 하나입니다. */
        const EditorViewTarget& getViewTarget( EditorViewKind kind ) const { return _arrViewTarget[static_cast<uint32>( kind )]; }
        /** @brief 게임 스레드 쪽 — 뷰 렌더 타깃을 그 크기로 (다시) 만듭니다. 같은 크기이면 아무 일도 하지 않습니다(`EditorUICommandKind::FitViewTarget` 의 적용). */
        void ensureViewTargetSize( EditorViewKind kind, uint32 width, uint32 height );
        /** @brief 뷰 렌더 타깃 하나를 해제합니다. 그 렌더 타깃을 그렸을 프레임의 GPU 작업이 끝난 뒤에 파괴됩니다. */
        void destroyViewTarget( EditorViewKind kind );
        /** @brief 뷰 렌더 타깃을 모두 해제합니다(미리보기 타깃 포함). */
        void destroyViewTargets();
        /**
         * @brief 패널이 이번 프레임에 그 뷰를 그렸다고 알립니다(UI 쪽). 셸은 넘긴 묶음에서 그린 뷰만 호스트에 요청합니다.
         * @param imageClientPos 이미지 왼쪽 위의 주 창 클라이언트 픽셀입니다(ImGui 화면 좌표에서 주 뷰포트 위치를 뺀 값 — 이 모델 층은 ImGui 를 모르므로 패널이 바꿔 넘긴다).
         *        그린 프레임마다 위치가 함께 오므로 낡지 않습니다.
         */
        void markViewDrawn( EditorViewKind kind, const float2& imageClientPos );
        /** @brief 이번 UI 프레임에 그 뷰를 그렸으면 true 입니다(UI 쪽 — 그리는 중인 묶음을 읽는다). */
        bool wasViewDrawn( EditorViewKind kind ) const { return _uiFrame.wasViewDrawn( kind ); }
        /** @brief 게임 스레드 쪽 — 마지막으로 넘긴 UI 프레임에서 호스트에 그 뷰의 렌더 타깃을 알릴지입니다(셸과 감시 값이 같은 판정을 쓴다). */
        bool isViewRequested( EditorViewKind kind ) const { return getPublishedUIFrame().isViewRequested( kind ); }
        /** @brief UI 프레임 시작(UI 쪽) — 그린 뷰 표시를 지우고, 게임 스레드가 마지막으로 넘긴 타깃 값을 이번 UI 프레임의 값으로 받습니다. */
        void clearViewDrawnMarks();
        /** @brief UI 쪽 — 이번 UI 프레임이 읽는 뷰 타깃 값입니다(프레임 시작에 받은 사본). */
        const EditorViewTargetState& getUITargetState( EditorViewKind kind ) const { return _uiTargets._arrView[static_cast<uint32>( kind )]; }
        /** @brief UI 쪽 — 이번 UI 프레임이 읽는 미리보기 타깃 값입니다. 게임 스레드가 아직 만들지 않았거나 놓았으면 nullptr 입니다. */
        const EditorPreviewTargetState* findUIPreviewTargetState( uint32 id ) const;

        /**
         * @brief UI 쪽 — 툴 창 하나의 미리보기 타깃 id 를 받습니다(1 부터, 다시 쓰지 않음). 어느 스레드에서나 부를 수 있습니다.
         * @details 슬롯과 렌더 타깃은 게임 스레드가 첫 `FitPreviewTarget` 명령에서 만들고 `FreePreviewTarget` 명령에서 놓는다 — UI 는 id 만 쥔다.
         */
        uint32 allocatePreviewTarget() { return _nextPreviewTargetID.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 게임 스레드 쪽 — 미리보기 타깃을 놓습니다(GPU 작업이 끝난 뒤 파괴). 모르는 id 면 아무것도 하지 않습니다. */
        void freePreviewTarget( uint32 id );
        /** @brief 게임 스레드 쪽 — 미리보기 타깃의 렌더 타깃을 그 크기로 (다시) 만듭니다. 슬롯이 없으면 만듭니다. 같은 크기이면 아무 일도 하지 않습니다. */
        void ensurePreviewTargetSize( uint32 id, uint32 width, uint32 height );
        /** @brief 게임 스레드 쪽 — 미리보기 타깃을 찾습니다. 없으면 nullptr 입니다. */
        const EditorPreviewViewTarget* findPreviewTarget( uint32 id ) const;
        /**
         * @brief 게임 스레드 쪽 — 엔진이 그사이 올린 렌더 텍스처를 받아 바꿔 끼우고(`applyIfReady`) 타깃 값 묶음을 UI 쪽으로 넘깁니다.
         * @details UI 명령을 적용한 뒤 프레임마다 한 번 부른다(`ImGuiEditor::applyUICommands`). 다음 UI 프레임 시작(`clearViewDrawnMarks`)이 받는다.
         */
        void applyReadyAndPublishTargets();
        /**
         * @brief 툴 창이 이번 프레임에 그 타깃을 보였고, 이 씬 · 이 눈으로 그려 달라고 알립니다(UI 쪽). 보인 프레임마다 부릅니다.
         * @details 한 프레임에 `kMaxPreviewSceneView` 개까지 받는다(호스트가 받는 수) — 넘치면 버리고 오류를 한 번 남긴다.
         */
        void submitPreviewView( uint32 id, uint32 sceneKey, const float4x4& viewProj, const float3& cameraPos );
        /** @brief 게임 스레드 쪽 — 넘긴 UI 프레임 @p frame 이 보인 미리보기 뷰를 호스트 형식으로 씁니다. 쓴 개수를 반환합니다(`ImGuiEditor::getPreviewSceneViews`). */
        uint32 collectSceneViews( const EditorUIFrameOutput& frame, PreviewSceneView* pOutView, uint32 capacity ) const;
        /** @brief 지금 가진 미리보기 타깃 수입니다(테스트용). */
        uint32 getPreviewTargetCount() const { return static_cast<uint32>( _listPreviewTarget.size() ); }

        /**
         * @brief 이번 프레임 뒤에 곧바로 다음 프레임이 필요하다고 알립니다(시간으로 움직이는 패널). 프레임을 시작할 때 지웁니다.
         * @details 요청이 없고 다른 까닭도 없으면 호스트 루프가 창 메시지까지 잔다(`EditorRedrawReason::Animation`). 미리보기 뷰를 낸 프레임은 스스로 요청한다(그 씬이 틱한다).
         */
        void requestAnimationFrame() { _uiFrame._bAnimationRequested = SW_TRUE; }
        /** @brief 이번 UI 프레임에 연속 프레임 요청이 있었으면 true 입니다(UI 쪽 — 셸이 넘기기 전에 다시 그릴 까닭으로 접는다). */
        bool isAnimationRequested() const { return _uiFrame._bAnimationRequested == SW_TRUE; }

    private:
        /** @brief 뷰 렌더 타깃 하나를 놓습니다(지금 것과 바꾸는 중인 것 모두). */
        void destroyTarget( EditorViewTarget& view );
        /** @brief 지금 그리는 타깃만 놓습니다(그리기 해제 큐가 이미 낸 프레임들 뒤로 미룬다). */
        void releaseCurrent( EditorViewTarget& view );
        /**
         * @brief 뷰 렌더 타깃 하나를 그 크기로 (다시) 요청하고, 엔진이 GPU 에 올렸으면 핸들을 받아 ImGui 에 등록합니다(프레임마다 부른다).
         * @details 에디터는 GPU 자원을 만들지 않는다 — 엔진의 렌더 텍스처(`TextureCache` 의 `rendertarget/` 경로, 언리얼 `UTextureRenderTarget2D`)를 크기를 알리고 빌린다.
         *          만들기는 엔진이 렌더 쪽 로드 펌프에서 한다. 예전에는 에디터 스레드가 RHI 팩터리로 직접 만들고 색을 채웠다(GL 컨텍스트를 다퉜다).
         */
        void ensureTargetSize( EditorViewTarget& view, uint32 width, uint32 height );
        /** @brief 엔진이 렌더 텍스처를 GPU 에 올렸으면 핸들을 받아 ImGui 에 등록합니다(받기 전에는 0 — 그리지 않는다). 게임 스레드가 프레임마다 부른다(`applyReadyAndPublishTargets`). */
        void applyIfReady( EditorViewTarget& view );
        /** @brief 미리보기 타깃을 찾습니다(고칠 수 있게). 없으면 nullptr 입니다. */
        EditorPreviewViewTarget* findPreviewTargetMutable( uint32 id );

    private:
        uint32                                 _nextTargetSerial{ 1 }; ///< 렌더 텍스처 경로의 번호(타깃 · 크기마다 새 경로)
        IRHIDevice*                            _pRHIDevice{ nullptr }; ///< 렌더 텍스처를 요청할 디바이스(셸이 알린다)
        ImGuiRenderer*                         _pRenderer{ nullptr };  ///< 렌더 타깃을 ImGui 텍스처로 등록하는 백엔드(셸이 알린다)
        EditorViewTarget                       _arrViewTarget[static_cast<uint32>( EditorViewKind::Count )]{};
        vector<EditorPreviewViewTarget>        _listPreviewTarget{};                    ///< 툴 창의 미리보기 타깃(게임 스레드 — 첫 `ensurePreviewTargetSize` 가 만든다)
        atomic<uint32>                         _nextPreviewTargetID{ 1 };               ///< 다음 미리보기 타깃 id(`allocatePreviewTarget` — UI 가 받는다)
        mutable mutex                          _targetStatesMutex{};                    ///< 넘긴 타깃 값 묶음을 지킨다 — 게임 스레드가 넘기고 UI 스레드가 받는다
        EditorTargetSnapshot _publishedTargets SW_GUARDED_BY( _targetStatesMutex ){};   ///< 게임 스레드가 마지막으로 넘긴 타깃 값(`applyReadyAndPublishTargets`)
        EditorTargetSnapshot                   _uiTargets{};                            ///< 이번 UI 프레임이 읽는 타깃 값(UI 스레드만 — `clearViewDrawnMarks` 가 받는다)
        EditorUIFrameOutput                    _uiFrame{};                              ///< 이번 UI 프레임이 쓰는 값 묶음(UI 스레드만)
        mutable mutex                          _publishedMutex{};                       ///< 넘긴 묶음을 지킨다 — UI 스레드가 넘기고 게임 스레드가 읽는다
        EditorUIFrameOutput _publishedFrame    SW_GUARDED_BY( _publishedMutex ){};      ///< 마지막으로 넘긴 UI 프레임 값 묶음(`publishUIFrame`)
        uint8                                  _bGameInputFocusedLastFrame{ SW_FALSE }; ///< 지난 프레임에 게임 뷰 · 게임 입력 면이 포커스였다(UI 쪽)
        uint8                                  _bPreviewOverflowLogged{ SW_FALSE };     ///< 미리보기 뷰가 한 프레임 상한을 넘었다고 이미 남겼다
    };
} // namespace sw::editor
