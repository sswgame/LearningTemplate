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
        string                   _id;
        string                   _title;
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

        /**
         * @brief 패널을 등록합니다. 메뉴에 보이는 이름은 패널의 `getPanelTitle()` 입니다.
         * @details 예전에는 `menuPath` 인자와 `_menuPath` 필드가 더 있었는데, **넘기는 곳도
         *          읽는 곳도 없었다** — 열아홉 개 등록이 전부 기본값이고 Window 메뉴는
         *          `_title` 로 항목을 만든다. 설정할 수는 있는데 아무 일도 하지 않는 손잡이라,
         *          다음 사람이 그것으로 메뉴를 옮기려다 시간을 버린다. 같이 있던 템플릿
         *          오버로드도 호출부가 하나도 없어 걷어냈다.
         */
        void registerPanel( unique_ptr<IEditorPanel> pPanel,
                            string_view              panelId,
                            EditorPanelCategory      category = EditorPanelCategory::Core );

        const vector<EditorPanelEntry>& getPanels() const { return _listPanel; }
        IEditorPanel*                   findPanel( string_view panelId ) const;
        bool                            setPanelOpen( string_view panelId, bool bOpen );
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
