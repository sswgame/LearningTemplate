/**
 * @file UserSettingsPanel.h
 * @brief 플레이어 옵션(사용자 설정)을 메뉴 UI 없이 바꿔 보는 개발용 창입니다. 게임 메뉴가 쓸 바인딩 API(`UserSettingsManager`)만 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

namespace sw
{
    struct UserSettingCategoryDef;
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
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "user_settings";

        UserSettingsPanel();
        ~UserSettingsPanel() override = default;

        /** @brief 카테고리 탭에 보이는 이름입니다(스키마 `text` 현지화 키, 없으면 id). 자체 테스트가 탭을 이 이름으로 찾는다. */
        [[nodiscard]] static const utf8* getCategoryLabel( const UserSettingCategoryDef& category );

        const utf8* getPanelTitle() const override { return "User Settings"; }
        void        drawContent() override;
        /** @brief 기본 폭(85 칸)은 범주 탭 아홉 개 중 뒤쪽(debug)을 스크롤 화살표 뒤로 숨겼다 — 엔진 범주가 한 줄에 들어가는 폭으로 연다. */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 125.0f, 28.0f ); }

    private:
        void drawToolbar( UserSettingsManager& settings );
        void drawSettingRow( UserSettingsManager& settings, const UserSettingDef& def );

    private:
        uint32 _selectedCategory;
        uint32 _lastRejectedCount; ///< 마지막 Apply 에서 대상이 거절한 설정 수(어느 설정인지는 매니저가 경고로 남긴다)
    };
} // namespace sw::editor
