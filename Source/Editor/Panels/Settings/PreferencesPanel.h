/**
 * @file PreferencesPanel.h
 * @brief 에디터 환경설정 창입니다(Edit > Preferences). 등록된 섹션(`SW_EDITOR_SETTINGS`)을 프로퍼티 그리드로 그리고, 바뀌면 잠시 뒤 저장합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"

namespace sw
{
    struct PropertyInfo;
} // namespace sw

namespace sw::editor
{
    struct EditorSettingsRegistration;

    /**
     * @class PreferencesPanel
     * @brief 언리얼 Editor Preferences · 유니티 Preferences · Godot Editor Settings 에 해당합니다. 왼쪽은 섹션 목록, 오른쪽은 고른 섹션의 그리드입니다.
     * @details 검색어는 모든 섹션을 훑어 맞는 프로퍼티가 있는 섹션만 목록에 남깁니다. 저장 단추는 없습니다 — 값이 바뀌면 0.5 초 뒤에 저장합니다(창을 닫아도).
     */
    class PreferencesPanel final : public IEditorPanel
    {
    public:
        PreferencesPanel();

        const utf8* getPanelTitle() const override { return "Preferences"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;
        void        shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 지난 프레임 목록에 보인 섹션 수입니다(탐침 `Editor.PreferencesVisibleSections`). */
        static uint32 getVisibleSectionCount();

    private:
        void onPropertyEdited( const PropertyInfo& prop );
        void savePendingChanges( bool bForce );

        EditorPropertyGrid                _grid;
        const EditorSettingsRegistration* _pSelected;
        int64                             _pendingSaveNanoseconds; ///< 저장을 기다리는 첫 편집 시각(0 이면 기다리지 않는다)
        bool                              _bModifiedOnly;
    };
} // namespace sw::editor
