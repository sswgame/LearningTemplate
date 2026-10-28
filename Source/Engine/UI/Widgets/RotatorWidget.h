/**
 * @file RotatorWidget.h
 * @brief 좌우로 하나씩 넘겨 고르는 선택 상자입니다(언리얼 CommonUI 의 `UCommonRotator`, 유니티의 `Slider` 로 만든 단계 선택, Godot 의 좌우 화살표 `OptionButton` 대용에 해당).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/ButtonWidget.h"

namespace sw
{
    class TextWidget;

    /**
     * @class RotatorWidget
     * @brief 고른 항목의 글을 가운데에 보이고 양옆에 화살표를 그립니다. 포커스를 가진 채 `UI.NavigateLeft` / `UI.NavigateRight` 가 항목을 하나씩 넘기고(끝에서 반대쪽 끝으로 돈다),
     *        `UI.Accept` 는 다음 항목입니다. 포인터는 왼쪽 화살표 쪽을 누르면 이전, 그 밖은 다음입니다. 오른쪽에서 왼쪽 위젯에서는 화면 기준 왼쪽 키가 다음 항목입니다.
     * @details 콤보 상자(`ComboBoxWidget`)는 펼쳐서 고르는 마우스 중심 위젯이고, 이 위젯은 패드 중심의 "좌우로 고르기"입니다. 설정 바인딩의 규약이 같아서(`_listOption`, `_selectedIndex`)
     *          옵션 메뉴의 열거형 행 견본(`engine/ui/parts/setting_enum.ui.xml`)에서 콤보 상자를 이 위젯으로 바꾸면 그대로 동작합니다. 항목이 둘보다 적으면 좌우 입력을 처리하지 않아
     *          포커스가 옆으로 떠납니다(항목이 하나뿐인 것은 고를 것이 없다).
     */
    REFLECT( Category = "UI", DisplayName = "Rotator", Tooltip = "Picks one option with left and right steps (pad friendly)" )
    class SW_API RotatorWidget : public ButtonWidget
    {
    public:
        REFLECT_BODY();

        /** @brief 고른 항목 알림입니다(항목 인덱스). */
        using SelectionDelegate = MulticastDelegate<void( uint32 )>;

        RotatorWidget();
        ~RotatorWidget() override;

        const TypeInfo* getTypeInfo() const override;
        /** @brief 바인딩이 쓴 필드에 맞춰 무효화합니다(항목과 고른 인덱스는 보이는 텍스트를 다시 만듭니다). */
        void onBoundPropertyChanged( const PropertyInfo& property ) override;

        /** @brief 항목들을 바꿉니다(고른 인덱스가 범위 밖이면 없음). */
        void                  setOptions( const vector<string>& listOption );
        const vector<string>& getOptions() const { return _listOption; }
        /** @brief 고른 인덱스를 바꿉니다(알림은 부르지 않으며, 코드가 정한 값입니다). 범위 밖이면 없음(`invalid_index::kUint32`). */
        void   setSelectedIndex( uint32 index );
        uint32 getSelectedIndex() const { return _selectedIndex; }
        /** @brief 선택기(스핀 버튼)로 내보입니다. 고른 항목이 값이고, 이름은 `accessibleName` 만 쓴다. */
        void               describeAccessible( AccessibleInfo& outInfo ) override;
        SelectionDelegate& getOnSelectionChanged() { return _onSelectionChanged; }

        /**
         * @brief 항목을 @p step 칸(-1 이전, +1 다음) 넘깁니다. 끝에서 반대쪽 끝으로 돌고, 아무것도 고르지 않았으면 처음(또는 끝)을 고릅니다.
         * @return 바뀌었으면 true 입니다(항목이 둘보다 적으면 false).
         */
        bool stepSelection( int32 step );

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;
        UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase ) override;

    protected:
        void handleClick() override;
        void paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const override;

    private:
        /** @brief 보이는 텍스트를 고른 항목으로 바꿉니다. */
        void refreshLabel();

    private:
        SelectionDelegate _onSelectionChanged;
        TextWidget*       _pLabel;           ///< 고른 항목을 그리는 자식입니다(이 위젯이 만들고 소유합니다).
        float32           _releaseLocalX;    ///< 마지막으로 뗀 위치의 가로(로컬)입니다. `handleClick` 이 어느 쪽을 눌렀는지 읽습니다.
        bool              _bHasReleasePoint; ///< `_releaseLocalX` 가 이번 클릭의 것인지 나타냅니다(`UI.Accept` 클릭에는 위치가 없다).
        PROPERTY( DisplayName = "Options" )
        vector<string> _listOption;
        PROPERTY( DisplayName = "Selected Index" )
        uint32 _selectedIndex;
    };
} // namespace sw
