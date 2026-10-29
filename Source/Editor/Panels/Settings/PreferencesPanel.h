/**
 * @file PreferencesPanel.h
 * @brief 에디터 환경설정 창입니다(Edit > Preferences). 등록된 섹션(`SW_EDITOR_SETTINGS`)을 프로퍼티 그리드로 그리고, 바뀌면 잠시 뒤 저장합니다.
 * @details 섹션 목록 · 고른 섹션 · 숨길 프로퍼티 · 되돌리기 · 저장 기다리기는 `PreferencesModel`(Model/, ImGui 없음)이 들고, 이 창은 목록 · 그리드 그리기와 검색 칸만 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Settings/Model/PreferencesModel.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class PreferencesPanel
     * @brief 언리얼 Editor Preferences · 유니티 Preferences · Godot Editor Settings 에 해당합니다. 왼쪽은 섹션 목록, 오른쪽은 고른 섹션의 그리드입니다.
     * @details 검색어는 모든 섹션을 훑어 맞는 프로퍼티가 있는 섹션만 목록에 남깁니다. 저장 단추는 없습니다 — 값이 바뀌면 첫 편집 0.5 초 뒤에 저장합니다(창을 닫아도).
     */
    class PreferencesPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "preferences";

        PreferencesPanel();

        const utf8* getPanelTitle() const override { return "Preferences"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        void        shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 지난 프레임 목록에 보인 섹션 수입니다(감시 값 `Editor.PreferencesVisibleSections`). */
        static uint32 getVisibleSectionCount();

    private:
        void onPropertyEdited( const PropertyInfo& prop );

        EditorPropertyGrid    _grid; ///< 검색 칸(`getFilterText`)도 든다
        PreferencesModel      _model;
        vector<hashed_string> _listHidden; ///< "Modified only" 가 숨길 프로퍼티(멤버로 두어 프레임마다 할당하지 않는다)
    };
} // namespace sw::editor
