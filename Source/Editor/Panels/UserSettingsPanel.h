/**
 * @file UserSettingsPanel.h
 * @brief 플레이어 옵션(사용자 설정)을 메뉴 UI 없이 바꿔 보는 개발용 창입니다. 게임 메뉴가 쓸 바인딩 API(`UserSettingsManager`)만 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
    struct UserSettingDef;

    class UserSettingsManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 카테고리 탭 · 설정 줄(종류별 위젯) · 적용 / 되돌리기 / 기본값 · 확인 카운트다운을 그립니다.
     * @details 게임 메뉴 UI 가 할 일의 견본이기도 합니다 — 값은 `getValue`(보류 우선), 바꾸기는 `setPendingValue`, 회색은 `isSettingEnabled`.
     */
    class UserSettingsPanel : public IEditorPanel
    {
    public:
        UserSettingsPanel();
        ~UserSettingsPanel() override = default;

        const utf8* getPanelTitle() const override { return "User Settings"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

    private:
        void drawToolbar( UserSettingsManager& settings );
        void drawSettingRow( UserSettingsManager& settings, const UserSettingDef& def );

    private:
        uint32 _selectedCategory;
    };
} // namespace sw::editor
