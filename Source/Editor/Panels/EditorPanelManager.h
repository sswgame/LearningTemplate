/**
 * @file EditorPanelManager.h
 * @brief 에디터 패널 인스턴스 중앙 등록 및 관리 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /** @brief 에디터 패널 카테고리 */
    enum class EditorPanelCategory : uint8
    {
        Core = 0, // Hierarchy, Inspector, GameView, Console, Profiler, ContentBrowser
        Tool,     // Sequencer, AnimationGraph, DialogueGraph, PrefabEditor, TileMap, SpriteClip
        Custom    // 게임/플러그인 커스텀 패널
    };

    /** @brief 등록된 에디터 패널 항목 메타데이터 */
    struct EditorPanelEntry
    {
        string                   _title;
        string                   _menuPath;
        EditorPanelCategory      _category{ EditorPanelCategory::Core };
        unique_ptr<IEditorPanel> _pInstance;
    };

    /**
     * @class EditorPanelManager
     * @brief 에디터 패널 인스턴스를 중앙에서 등록 및 관리하는 클래스 (EditorContext 소유)
     */
    class EditorPanelManager
    {
    public:
        EditorPanelManager()  = default;
        ~EditorPanelManager() = default;

        void registerPanel( unique_ptr<IEditorPanel> pPanel,
                            EditorPanelCategory      category = EditorPanelCategory::Core,
                            string_view              menuPath = {} );

        template <typename TPanel, typename... TArgs>
        TPanel* registerPanel( EditorPanelCategory category = EditorPanelCategory::Core,
                               string_view         menuPath = {}, TArgs&&... args )
        {
            unique_ptr<TPanel> pPanel = make_unique<TPanel>( std::forward<TArgs>( args )... );
            TPanel*            pRaw   = pPanel.get();
            registerPanel( std::move( pPanel ), category, menuPath );
            return pRaw;
        }

        const vector<EditorPanelEntry>& getPanels() const { return _listPanel; }
        vector<EditorPanelEntry>&       getPanelsMutable() { return _listPanel; }
        IEditorPanel*                   findPanel( string_view title ) const;
        bool                            setPanelOpen( string_view title, bool bOpen );
        void                            clear();
        void                            registerDefaultPanels();
        void                            drawOpenPanels();
        void                            preRenderOpenPanels( IRHIDevice* pRhiDevice );
        void                            shutdownAllPanels( IRHIDevice* pRhiDevice );
        /** @brief 포커스된 도구 문서가 dirty이면 저장하고 true입니다. */
        bool saveFocusedDirtyDocument();
        /** @brief 모든 더티 도구 문서를 저장합니다. 하나라도 실패하면 false입니다. */
        bool saveAllDirtyDocuments();
        /** @brief 열린/닫힌 패널을 포함해 더티 문서 개수입니다. */
        uint32 countDirtyDocuments() const;
        /** @brief 모든 더티 도구 문서를 저장하지 않고 버립니다. */
        void discardAllDirtyDocuments();

    private:
        vector<EditorPanelEntry> _listPanel;
    };
} // namespace sw::editor
