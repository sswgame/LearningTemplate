/**
 * @file UserSettingsPanel.h
 * @brief 플레이어 옵션(사용자 설정)을 메뉴 UI 없이 바꿔 보는 개발용 창입니다. 값은 `UserSettingsViewModel::gather` 가 모으고 명령도 그쪽이 받으며, 이 패널은 그리기만 합니다(MVVM 계약).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Settings/Model/UserSettingsViewModel.h"

namespace sw::editor
{
    /**
     * @brief 카테고리 탭 · 설정 줄(종류별 위젯) · 적용 / 되돌리기 / 기본값 · 확인 카운트다운을 그립니다.
     * @details 게임 메뉴 UI 가 할 일의 견본이기도 합니다 — 바인딩 API 의 쓰임은 `UserSettingsViewModel` 머리말에 있다.
     */
    class UserSettingsPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "user_settings";

        UserSettingsPanel();
        ~UserSettingsPanel() override = default;

        const utf8* getPanelTitle() const override { return "User Settings"; }
        void        drawContent() override;
        /** @brief 기본 폭(85 칸)은 범주 탭 아홉 개 중 뒤쪽(debug)을 스크롤 화살표 뒤로 숨겼다 — 엔진 범주가 한 줄에 들어가는 폭으로 연다. */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 125.0f, 28.0f ); }

    private:
        void drawToolbar();
        void drawSettingRow( const UserSettingRow& row );

    private:
        UserSettingsViewModel _viewModel;
        UserSettingsViewState _viewState;        ///< 프레임마다 다시 채운다
        uint32                _selectedCategory; ///< 지난 프레임에 연 탭(기본값 단추가 고른다 — 보기 상태)
    };
} // namespace sw::editor
