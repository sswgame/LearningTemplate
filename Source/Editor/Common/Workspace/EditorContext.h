#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    class SelectionManager;
    class EditorWorkspace;
    class EditorNotificationManager;
    class EditorActionMenuManager;
    class EditorPanelManager;
    class EditorPopupManager;
    class AssetEditorManager;
    class InspectorComponentManager;
    class InspectorPropertyManager;
    class IImGuiRendererBackend;

    /** @brief 에디터가 소유하는 Game View RT. App은 매 프레임 핸들만 조회합니다. */
    struct EditorGameView
    {
        uint64 _renderTarget{ 0 };
        void*  _pTextureId{ nullptr };
        uint32 _width{ 0 };
        uint32 _height{ 0 };
    };

    /**
     * @class EditorContext
     * @brief 에디터 셸(ImGuiEditor)이 생명주기를 직접 생성/소멸 관리하는 에디터 중앙 컨텍스트
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
        static EditorContext* get() { return s_pActiveContext; }
        /** @brief 활성 에디터 컨텍스트 포인터를 설정합니다. */
        static void setActive( EditorContext* pContext ) { s_pActiveContext = pContext; }

        SelectionManager&          getSelectionManager() { return *_pSelectionManager; }
        EditorWorkspace&           getWorkspace() { return *_pWorkspace; }
        EditorNotificationManager& getNotificationManager() { return *_pNotificationManager; }
        EditorActionMenuManager&   getActionMenuManager() { return *_pActionMenuManager; }
        EditorPanelManager&        getPanelManager() { return *_pPanelManager; }
        EditorPopupManager&        getPopupManager() { return *_pPopupManager; }
        AssetEditorManager&        getAssetEditorManager() { return *_pAssetEditorManager; }
        InspectorComponentManager& getInspectorComponentManager() { return *_pInspectorComponentManager; }
        InspectorPropertyManager&  getInspectorPropertyManager() { return *_pInspectorPropertyManager; }

        void        setRhiDevice( IRHIDevice* pDevice ) { _pRhiDevice = pDevice; }
        IRHIDevice* getRhiDevice() const { return _pRhiDevice; }
        void        setRendererBackend( IImGuiRendererBackend* pBackend ) { _pRendererBackend = pBackend; }
        void        setGameViewHovered( bool bHovered ) { _bGameViewHovered = bHovered ? SW_TRUE : SW_FALSE; }
        void        setGameViewFocused( bool bFocused ) { _bGameViewFocused = bFocused ? SW_TRUE : SW_FALSE; }
        bool        isGameViewHovered() const { return _bGameViewHovered == SW_TRUE; }
        bool        isGameViewFocused() const { return _bGameViewFocused == SW_TRUE; }

        const EditorGameView& getGameView() const { return _gameView; }
        void                  ensureGameViewSize( uint32 width, uint32 height );
        void                  destroyGameView();

    private:
        unique_ptr<SelectionManager>          _pSelectionManager;
        unique_ptr<EditorWorkspace>           _pWorkspace;
        unique_ptr<EditorNotificationManager> _pNotificationManager;
        unique_ptr<EditorActionMenuManager>   _pActionMenuManager;
        unique_ptr<EditorPanelManager>        _pPanelManager;
        unique_ptr<EditorPopupManager>        _pPopupManager;
        unique_ptr<AssetEditorManager>        _pAssetEditorManager;
        unique_ptr<InspectorComponentManager> _pInspectorComponentManager;
        unique_ptr<InspectorPropertyManager>  _pInspectorPropertyManager;
        IRHIDevice*                           _pRhiDevice;
        IImGuiRendererBackend*                _pRendererBackend;
        EditorGameView                        _gameView;

        uint8                  _bGameViewHovered : 1;
        uint8                  _bGameViewFocused : 1;
        [[maybe_unused]] uint8 _reserved         : 6;

        static EditorContext* s_pActiveContext;
    };
} // namespace sw::editor
