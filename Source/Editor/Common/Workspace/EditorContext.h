#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorViewTargetUtil.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    class AssetHotReload;
    class ConfigHotReload;
    class EditorAssetValidation;
    class EditorCommandRegistry;
    class EditorDefaultObjects;
    class EditorDockLayout;
    class EditorNetPlaySession;
    class EditorNotificationManager;
    class EditorPanelManager;
    class EditorPopupManager;
    class EditorSelection;
    class EditorSourceControl;
    class EditorWorkspace;
    class IEditorPanelHost;
    class IImGuiRendererBackend;
    class InspectorComponentManager;
    class InspectorPropertyManager;

    /** @brief 에디터가 소유하는 뷰 렌더 타깃 하나입니다. 씬 뷰와 게임 뷰가 각각 하나씩 가집니다. 호스트는 프레임마다 핸들만 조회합니다(`ImGuiEditor::getSceneViewport`, `getGameViewport`). */
    struct EditorViewTarget
    {
        uint64 _renderTarget{ 0 };
        void*  _pTextureID{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
        uint8  _bDrawn{ SW_FALSE }; ///< 이번 UI 프레임에 패널이 이 뷰를 그렸다(보인다). 프레임을 시작할 때 지운다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorContext
     * @brief 에디터의 중앙 컨텍스트입니다. 에디터 셸(ImGuiEditor)이 직접 만들고 파괴합니다.
     */
    class SW_EDITOR_API EditorContext
    {
    public:
        EditorContext();
        ~EditorContext();

        /** @brief 에디터 서브시스템들을 만들고 초기화합니다. */
        void initialize();
        /** @brief 에디터 서브시스템들을 정리하고 해제합니다. */
        void shutdown();
        /**
         * @brief 패널, 팝업, 인스펙터 매니저를 등록 목록과 맞춥니다. 에디터 프레임 앞에서 호출합니다.
         * @details 확장 모듈은 에디터가 뜬 뒤에도 로드되고 언로드된다(핫 리로드). 등록 세대가 같으면 비교 한 번으로 끝난다.
         */
        void syncExtensionRegistrations();

        /**
         * @brief 현재 활성화된 전역 에디터 컨텍스트 포인터를 반환합니다. 셸이 `initialize` 에서 설정하고 `shutdown` 에서 해제합니다. 없으면 nullptr 입니다.
         * @details 접근 경로는 이 함수 하나다. 에디터 범위(`getService<EditorContext>`)에 다시 걸지 않는다. 호스트(엔진) 서비스는 `EditorService.h` 의
         *          `getService<T>()` 가, 에디터가 소유한 하위 시스템은 이 컨텍스트의 접근자가 반환한다.
         */
        static EditorContext* get();
        /** @brief 활성 에디터 컨텍스트를 설정합니다. 그 컨텍스트의 범위가 지금 에디터 범위가 된다(`editor::getService<T>()` 가 거기서 찾는다). */
        static void setActive( EditorContext* pContext );

        /**
         * @brief 에디터 범위의 하위 객체 @p T 입니다(`EditorSelection` · `EditorWorkspace` · `EditorPanelManager` · `IEditorPanelHost` …). `initialize` 뒤에는 늘 있다.
         * @details 언리얼 `GEditor->GetEditorSubsystem<T>()` 자리다. 범위가 타입으로 찾는다(하위 객체마다 포인터 · 접근자를 두지 않는다).
         *          `initialize` 전이나 `shutdown` 뒤에 부르면 부른 쪽의 잘못이라 `SW_ASSERT` 로 멈춘다 — 없을 수 있는 자리는 `findSubsystem` 을 쓴다.
         *          조회는 하위 객체 수만큼의 선형 검색이다 — 프레임마다 많이 부르는 자리는 한 번 얻은 참조를 든다.
         */
        template <class T>
        T& getSubsystem() const
        {
            return _scope.get<T>();
        }
        /** @brief 에디터 범위의 하위 객체 @p T 입니다. 없으면 nullptr 입니다. */
        template <class T>
        T* findSubsystem() const
        {
            return _scope.find<T>();
        }
        /** @brief 에디터 셸의 도킹 레이아웃을 반환합니다. 소유자는 `ImGuiEditor` 입니다. 셸이 만들어지기 전이면 nullptr 입니다. */
        EditorDockLayout* findDockLayout() const { return _pDockLayout; }
        /** @brief 셸이 도킹 레이아웃을 알립니다. 종료할 때는 nullptr 을 넘깁니다. */
        void setDockLayout( EditorDockLayout* pDockLayout ) { _pDockLayout = pDockLayout; }

        void        setRHIDevice( IRHIDevice* pDevice ) { _pRHIDevice = pDevice; }
        IRHIDevice* getRHIDevice() const { return _pRHIDevice; }
        void        setRendererBackend( IImGuiRendererBackend* pBackend ) { _pRendererBackend = pBackend; }
        /** @brief ImGui 렌더러 백엔드를 반환합니다. 텍스처를 ImGui 에 등록하려는 패널이 씁니다. 없으면 nullptr 입니다. */
        IImGuiRendererBackend* getRendererBackend() const { return _pRendererBackend; }
        void                   setGameViewHovered( bool bHovered ) { _bGameViewHovered = bHovered ? SW_TRUE : SW_FALSE; }
        void                   setGameViewFocused( bool bFocused ) { _bGameViewFocused = bFocused ? SW_TRUE : SW_FALSE; }
        bool                   isGameViewHovered() const { return _bGameViewHovered == SW_TRUE; }
        bool                   isGameViewFocused() const { return _bGameViewFocused == SW_TRUE; }

        /** @brief 뷰 RT 하나입니다. */
        const EditorViewTarget& getViewTarget( EditorViewKind kind ) const { return _arrViewTarget[static_cast<uint32>( kind )]; }
        /** @brief 뷰 렌더 타깃을 그 크기로 (다시) 만듭니다. 같은 크기이면 아무 일도 하지 않습니다. */
        void ensureViewTargetSize( EditorViewKind kind, uint32 width, uint32 height );
        /** @brief 뷰 렌더 타깃 하나를 해제합니다. 그 렌더 타깃을 그렸을 프레임의 GPU 작업이 끝난 뒤에 파괴됩니다. */
        void destroyViewTarget( EditorViewKind kind );
        /** @brief 뷰 렌더 타깃을 모두 해제합니다. */
        void destroyViewTargets();
        /** @brief 패널이 이번 프레임에 그 뷰를 그렸다고 알립니다. 셸은 그린 뷰만 호스트에 요청합니다. */
        void markViewDrawn( EditorViewKind kind ) { _arrViewTarget[static_cast<uint32>( kind )]._bDrawn = SW_TRUE; }
        /** @brief 이번 프레임에 그 뷰를 그렸으면 true 입니다. */
        bool wasViewDrawn( EditorViewKind kind ) const { return getViewTarget( kind )._bDrawn == SW_TRUE; }
        /** @brief UI 프레임을 시작할 때 그린 뷰 표시를 지웁니다. */
        void clearViewDrawnMarks();

        /** @brief 플레이(PIE) 세션 상태를 반환합니다. 조작은 `EditorPlaySession` 을 거칩니다. */
        PlaySessionData& getPlaySessionData() { return _playSessionData; }

        /** @brief 현재 적용된 테마를 반환합니다. 조작은 `EditorThemeUtil` 을 거칩니다. */
        EditorThemeConfig& getThemeConfig() { return _themeConfig; }

    private:
        /** @brief 에디터 범위 — 아래 하위 객체(`EditorSubsystem`)를 소유하고 타입으로 찾게 합니다. 선언된 의존 순서로 초기화하고 역순으로 종료 · 소멸합니다. `getSubsystem<T>()` 가 여기서 찾는다. */
        SubsystemScope         _scope;
        IRHIDevice*            _pRHIDevice;
        EditorDockLayout*      _pDockLayout;
        IImGuiRendererBackend* _pRendererBackend;
        EditorViewTarget       _arrViewTarget[static_cast<uint32>( EditorViewKind::Count )];
        PlaySessionData        _playSessionData;
        EditorThemeConfig      _themeConfig;

        uint8                  _bGameViewHovered : 1;
        uint8                  _bGameViewFocused : 1;
        [[maybe_unused]] uint8 _reserved         : 6;

        static EditorContext* s_pActiveContext;
    };
} // namespace sw::editor
