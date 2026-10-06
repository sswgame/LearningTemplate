/**
 * @file PanelWidget.h
 * @brief 자식을 소유하고 배치하는 위젯입니다(언리얼 SPanel · UMG UPanelWidget · Godot Container).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/Widget.h"

namespace sw
{
    /**
     * @class PanelWidget
     * @brief 자식을 드는 위젯입니다. 자식 순서가 그리기 순서이고(뒤가 위 — z 순서를 둔 패널은 `collectPaintOrder`), 히트 테스트는 그 역순입니다.
     * @details 자식을 붙이고 떼면 자기 kLayout 입니다. 트리에 붙어 있으면 자식도 함께 붙고 떨어집니다(번호 · 이름표).
     */
    REFLECT( Abstract )
    class SW_API PanelWidget : public Widget
    {
    public:
        REFLECT_BODY();

        PanelWidget();
        ~PanelWidget() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 자식을 끝에 붙이고 그 포인터를 돌려줍니다(소유는 패널). kLayout 무효화. */
        Widget* addChild( unique_ptr<Widget> child );
        /** @brief 자식을 @p index 자리에 끼웁니다(끝을 넘으면 끝에). */
        Widget* insertChild( uint32 index, unique_ptr<Widget> child );
        /** @brief 자식을 떼어 소유를 돌려줍니다(트리에서 떨어진다 — 포커스가 그 아래에 있으면 트리가 푼다). 자식이 아니면 nullptr 입니다. */
        unique_ptr<Widget> removeChild( Widget* pChild );
        /** @brief 자식을 모두 떼어 지웁니다. */
        void   clearChildren();
        uint32 getChildCount() const { return static_cast<uint32>( _listChild.size() ); }
        /** @brief @p index 번째 자식입니다(범위는 부르는 쪽이 지킨다). */
        Widget* getChild( uint32 index ) const { return _listChild[index].get(); }
        /** @brief @p pChild 의 자식 자리입니다. 자식이 아니면 `invalid_index::kUint32` 입니다. */
        uint32 findChildIndex( const Widget* pChild ) const;
        /**
         * @brief 그리기 순서가 자식 순서와 다를 수 있는 패널이면 true 입니다(z 순서 — `CanvasPanel`). false 면 그리기 순서 = 자식 순서라 목록을 만들지 않는다.
         */
        virtual bool hasCustomPaintOrder() const { return false; }
        /** @brief 그리는 순서(뒤가 위)의 자식 자리를 담습니다. 히트 테스트는 그 역순입니다. 기본은 자식 순서입니다. */
        virtual void collectPaintOrder( vector<uint32>& outListIndex ) const;
        /** @brief 자식을 자르는가(스크롤 패널). 히트 테스트와 그리기가 함께 따른다. */
        bool clipsChildren() const { return _bClipChildren; }
        /** @brief 자른 자손을 내용을 옮겨 보이게 할 수 있는 패널이면 true 입니다(스크롤 패널) — 포커스 탐색은 이 패널이 자른 자손도 후보로 본다. */
        virtual bool canScrollIntoView() const { return false; }
        /** @brief 자손 @p widget 이 보이도록 내용을 옮깁니다(포커스 탐색 뒤 — `UiFocusManager::navigate`). 옮겼으면 true 입니다. 기본은 아무것도 하지 않습니다. */
        virtual bool scrollIntoView( const Widget& widget );
        void         setClipChildren( bool bClip );

    protected:
        /**
         * @brief 자식을 배치합니다(레이아웃 — 패널마다 다르다). 각 자식에 `arrangeChild( context, child, 위치, 크기 )` 를 부릅니다. 기본은 아무것도 하지 않습니다.
         * @details 자식의 원하는 크기(`getDesiredSize`)는 같은 걷기의 `computeDesiredSize` 가 이미 쟀습니다. @p size 는 이 패널 자기 크기입니다.
         */
        virtual void arrangeChildren( const UiLayoutContext& context, const float2& size );
        /** @brief 자식 하나를 이 패널의 로컬 슬롯 사각형에 놓습니다 — 자식 슬롯의 여백 · 정렬을 적용하고 자식의 자식까지 놓습니다(`UiLayoutPass::arrange`). */
        void arrangeChild( const UiLayoutContext& context, Widget& child, const float2& localPosition, const float2& size );

    private:
        friend class Widget;
        friend class WidgetTree;
        friend class UiLayoutPass;
        friend class UiPaintPass;

        vector<unique_ptr<Widget>> _listChild;
        PROPERTY( DisplayName = "Clip Children" )
        bool _bClipChildren;
    };
} // namespace sw
