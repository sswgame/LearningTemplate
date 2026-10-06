/**
 * @file ComboBoxWidget.h
 * @brief 펼쳐서 하나를 고르는 목록 상자입니다(UMG ComboBox · 유니티 DropdownField · Godot OptionButton).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/Widgets/ButtonWidget.h"

namespace sw
{
    class TextWidget;

    /**
     * @class ComboBoxWidget
     * @brief 고른 항목의 글을 보이는 버튼입니다. 클릭(또는 포커스 상태의 `UI.Accept`)하면 **팝업 화면**(Modal 층 · 막지 않음 — 화면 스택의 화면 하나)을 열어
     *        항목 버튼을 상자 바로 아래에 세로로 놓습니다. 항목을 고르면 값을 바꾸고 `getOnSelectionChanged()` 를 부른 뒤 닫고, 팝업 밖 클릭 · `UI.Back` 은 그냥 닫습니다.
     * @details 팝업은 이 위젯이 든 화면의 `UiSystem` 에 올립니다(화면 밖의 트리면 열지 않는다). 팝업은 상자를 화면 핸들 · 위젯 번호로 다시 찾습니다 — 그사이 상자가
     *          지워졌으면 고른 값을 버린다. 탐색 방식이면 고른 항목에 포커스가 갑니다.
     */
    REFLECT( Category = "UI", DisplayName = "Combo Box", Tooltip = "Button that opens a popup list to pick one option" )
    class SW_API ComboBoxWidget : public ButtonWidget
    {
    public:
        REFLECT_BODY();

        /** @brief 고른 항목 알림입니다(항목 자리). */
        using SelectionDelegate = MulticastDelegate<void( uint32 )>;

        ComboBoxWidget();
        ~ComboBoxWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 칸에 맞춰 무효화합니다(항목 · 고른 자리는 보이는 글을 다시). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        /** @brief 항목들을 바꿉니다(고른 자리가 범위 밖이면 없음). */
        void                  setOptions( const vector<string>& listOption );
        const vector<string>& getOptions() const { return _listOption; }
        /** @brief 고른 자리를 바꿉니다(알림은 부르지 않는다 — 코드가 정한 값). 범위 밖이면 없음(`invalid_index::kUint32`). */
        void               setSelectedIndex( uint32 index );
        uint32             getSelectedIndex() const { return _selectedIndex; }
        SelectionDelegate& getOnSelectionChanged() { return _onSelectionChanged; }
        /** @brief 팝업이 열려 있으면 그 화면 핸들입니다(없으면 무효). */
        UiScreenHandle getPopupScreen() const { return _popupScreen; }
        /** @brief 팝업이 고른 항목을 받습니다(값 · 알림 · 팝업 핸들 비우기). 팝업 화면이 부른다. */
        void choosePopupOption( uint32 index );

    protected:
        void handleClick() override;
        void paintOverChildren( CanvasPainter& painter, const UiPaintContext& context ) const override;

    private:
        /** @brief 이 위젯이 든 화면의 UI 시스템에 팝업을 올립니다. */
        void openPopup();
        /** @brief 보이는 글을 고른 항목으로 바꿉니다. */
        void refreshLabel();

    private:
        SelectionDelegate _onSelectionChanged;
        TextWidget*       _pLabel; ///< 고른 항목을 그리는 자식(이 위젯이 만들고 소유한다)
        UiScreenHandle    _popupScreen;
        PROPERTY( DisplayName = "Options" )
        vector<string> _listOption;
        PROPERTY( DisplayName = "Selected Index" )
        uint32 _selectedIndex;
    };
} // namespace sw
