#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"

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
    class EditorDockLayout;
    class EditorNotificationManager;
    class EditorPanelManager;
    class EditorPopupManager;
    class EditorSelection;
    class EditorSourceControl;
    class EditorWorkspace;
    class IImGuiRendererBackend;
    class InspectorComponentManager;
    class InspectorPropertyManager;

    /** @brief 에디터가 소유하는 Game View RT 입니다. App 은 프레임마다 핸들만 조회합니다. */
    struct EditorGameView
    {
        uint64 _renderTarget{ 0 };
        void*  _pTextureId{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorContext
     * @brief 에디터의 중앙 컨텍스트입니다. 에디터 셸(ImGuiEditor)이 직접 만들고 없앱니다.
     */
    class EditorContext
    {
    public:
        EditorContext();
        ~EditorContext();

        /** @brief 에디터 서브시스템들을 생성 및 초기화합니다. */
        void initialize();
        /** @brief 에디터 서브시스템들을 정리 및 해제합니다. */
        void shutdown();

        /** @brief 현재 활성화된 전역 에디터 컨텍스트 포인터를 반환합니다. */
        static EditorContext* get();
        /** @brief 활성 에디터 컨텍스트 포인터를 설정합니다. */
        static void setActive( EditorContext* pContext ) { s_pActiveContext = pContext; }

        EditorSelection&           getEditorSelection() { return *_pEditorSelection; }
        EditorWorkspace&           getWorkspace() { return *_pWorkspace; }
        EditorNotificationManager& getNotificationManager() { return *_pNotificationManager; }
        EditorCommandRegistry&     getCommandRegistry() { return *_pCommandRegistry; }
        EditorPanelManager&        getPanelManager() { return *_pPanelManager; }
        EditorPopupManager&        getPopupManager() { return *_pPopupManager; }
        AssetHotReload&            getAssetHotReload() { return *_pAssetHotReload; }
        /** @brief `Config/` 설정 파일 감시입니다. */
        ConfigHotReload& getConfigHotReload() { return *_pConfigHotReload; }
        /** @brief 에디터 셸의 도킹 레이아웃입니다(소유는 `ImGuiEditor`). 셸이 서기 전이면 nullptr 입니다. */
        EditorDockLayout* findDockLayout() const { return _pDockLayout; }
        /** @brief 셸이 도킹 레이아웃을 알립니다(내릴 때 nullptr). */
        void setDockLayout( EditorDockLayout* pDockLayout ) { _pDockLayout = pDockLayout; }
        /** @brief 저장 · 임포트 직후 에셋 검증입니다. */
        EditorAssetValidation& getAssetValidation() { return *_pAssetValidation; }
        /** @brief 버전 관리 잠금(체크아웃) 창구입니다. */
        EditorSourceControl&       getSourceControl() { return *_pSourceControl; }
        InspectorComponentManager& getInspectorComponentManager() { return *_pInspectorComponentManager; }
        InspectorPropertyManager&  getInspectorPropertyManager() { return *_pInspectorPropertyManager; }

        void        setRhiDevice( IRHIDevice* pDevice ) { _pRhiDevice = pDevice; }
        IRHIDevice* getRhiDevice() const { return _pRhiDevice; }
        void        setRendererBackend( IImGuiRendererBackend* pBackend ) { _pRendererBackend = pBackend; }
        /** @brief ImGui 렌더러 백엔드입니다. 텍스처를 ImGui 에 등록하려는 패널이 씁니다. 없으면 nullptr 입니다. */
        IImGuiRendererBackend* getRendererBackend() const { return _pRendererBackend; }
        void                   setGameViewHovered( bool bHovered ) { _bGameViewHovered = bHovered ? SW_TRUE : SW_FALSE; }
        void                   setGameViewFocused( bool bFocused ) { _bGameViewFocused = bFocused ? SW_TRUE : SW_FALSE; }
        bool                   isGameViewHovered() const { return _bGameViewHovered == SW_TRUE; }
        bool                   isGameViewFocused() const { return _bGameViewFocused == SW_TRUE; }

        const EditorGameView& getGameView() const { return _gameView; }
        void                  ensureGameViewSize( uint32 width, uint32 height );
        void                  destroyGameView();

        /** @brief 플레이(PIE) 세션 상태입니다. 조작은 `EditorPlaySession` 을 통합니다. */
        PlaySessionData& getPlaySessionData() { return _playSessionData; }

        /** @brief 지금 적용된 테마입니다. 조작은 `EditorThemeUtil` 을 통합니다. */
        EditorThemeConfig& getThemeConfig() { return _themeConfig; }

    private:
        unique_ptr<EditorSelection>           _pEditorSelection;
        unique_ptr<EditorWorkspace>           _pWorkspace;
        unique_ptr<EditorNotificationManager> _pNotificationManager;
        unique_ptr<EditorCommandRegistry>     _pCommandRegistry;
        unique_ptr<EditorPanelManager>        _pPanelManager;
        unique_ptr<EditorPopupManager>        _pPopupManager;
        unique_ptr<AssetHotReload>            _pAssetHotReload;
        unique_ptr<ConfigHotReload>           _pConfigHotReload;
        unique_ptr<EditorAssetValidation>     _pAssetValidation;
        unique_ptr<EditorSourceControl>       _pSourceControl;
        unique_ptr<InspectorComponentManager> _pInspectorComponentManager;
        unique_ptr<InspectorPropertyManager>  _pInspectorPropertyManager;
        IRHIDevice*                           _pRhiDevice;
        EditorDockLayout*                     _pDockLayout;
        IImGuiRendererBackend*                _pRendererBackend;
        EditorGameView                        _gameView;
        PlaySessionData                       _playSessionData;
        EditorThemeConfig                     _themeConfig;

        uint8                  _bGameViewHovered : 1;
        uint8                  _bGameViewFocused : 1;
        [[maybe_unused]] uint8 _reserved         : 6;

        static EditorContext* s_pActiveContext;
    };
} // namespace sw::editor
