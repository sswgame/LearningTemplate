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
#include "Editor/Common/Workspace/EditorRegistry.h"

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
        Tool,     // Sequencer, AnimGraph, DialogueGraph, Prefab, TileMap, SpriteClip
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
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPanelRegistration
     * @brief 패널 한 종류의 등록 줄입니다. 패널의 .cpp 가 `SW_EDITOR_PANEL` 로 둡니다.
     * @details `_pId` 는 `windows.ini` 의 가시성 키이자 `-gv_editorOpenPanel` 의 값입니다. `_order` 는 Panel 메뉴 · 그리기 순서이고,
     *          카테고리는 Panel 메뉴의 Panels / Tools 묶음을 정합니다.
     */
    struct EditorPanelRegistration : EditorRegistration
    {
        static constexpr const utf8* kKindName = "panel";

        EditorPanelCategory _category;
        unique_ptr<IEditorPanel> ( *_pCreate )();
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 등록 줄이 가리키는 패널 생성 함수입니다. */
    template <typename TPanel>
    unique_ptr<IEditorPanel> createEditorPanel()
    {
        return make_unique<TPanel>();
    }

    /**
     * @class EditorPanelManager
     * @brief 에디터 패널 인스턴스를 한곳에서 등록하고 관리합니다(EditorContext 소유).
     */
    class EditorPanelManager
    {
    public:
        EditorPanelManager()  = default;
        ~EditorPanelManager() = default;

        /** @brief 패널을 등록합니다. 메뉴에 보이는 이름은 패널의 `getPanelTitle()` 입니다. */
        void registerPanel( unique_ptr<IEditorPanel> pPanel,
                            string_view              panelId,
                            EditorPanelCategory      category = EditorPanelCategory::Core );

        const vector<EditorPanelEntry>& getPanels() const { return _listPanel; }
        IEditorPanel*                   findPanel( string_view panelId ) const;
        bool                            setPanelOpen( string_view panelId, bool bOpen );
        void                            clear();
        /** @brief `SW_EDITOR_PANEL` 로 등록된 패널을 순서대로 만들어 둡니다(앞의 목록은 버립니다). */
        void registerDefaultPanels();
        void drawOpenPanels();
        void preRenderOpenPanels( IRHIDevice* pRhiDevice );
        void shutdownAllPanels( IRHIDevice* pRhiDevice );
        /** @brief 포커스된 도구 문서가 dirty이면 저장하고 true입니다. */
        [[nodiscard]] bool saveFocusedDirtyDocument();
        /** @brief 모든 더티 도구 문서를 저장합니다. 하나라도 실패하면 false입니다. */
        [[nodiscard]] bool saveAllDirtyDocuments();
        /** @brief 열린 패널 · 닫힌 패널을 모두 포함한 dirty 문서 개수입니다. */
        uint32 countDirtyDocuments() const;
        /** @brief 모든 더티 도구 문서를 저장하지 않고 버립니다. */
        void discardAllDirtyDocuments();

    private:
        vector<EditorPanelEntry> _listPanel;
    };
} // namespace sw::editor

/**
 * @brief 패널 종류를 그 패널의 .cpp 에서 등록합니다. 예: `SW_EDITOR_PANEL( HierarchyPanel, "hierarchy", EditorPanelCategory::Core, 100 );`
 * @param TPanel   기본 생성자가 있는 `IEditorPanel` 구현
 * @param pId      가시성 저장 키(리터럴, 종류 안에서 유일)
 * @param category Panel 메뉴의 묶음
 * @param order    Panel 메뉴 · 그리기 순서(작을수록 앞)
 */
#define SW_EDITOR_PANEL( TPanel, pId, category, order ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorPanelRegistration, Panel_##TPanel, { pId, order }, category, &::sw::editor::createEditorPanel<TPanel> )
