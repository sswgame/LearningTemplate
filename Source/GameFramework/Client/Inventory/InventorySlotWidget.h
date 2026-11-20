/**
 * @file GameFramework/Client/Inventory/InventorySlotWidget.h
 * @brief 인벤토리 화면의 칸 단추입니다. 왼쪽 누름 · 끌어 놓기 · 오른쪽 누름 · 길게 누르기를 화면(`InventoryScreen`)에 알립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Widgets/ButtonWidget.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class InventoryScreen;

    /**
     * @class InventorySlotWidget
     * @brief 가방 칸 하나의 단추입니다. 클릭 · 확인(패드 A)은 단추 명령 `Slot` 이고, 끌어 다른 칸에 놓으면 옮기기, 오른쪽 누름은 동작 메뉴입니다.
     * @details 엔진 UI 에는 끌어 놓기 이벤트가 없다 — 누른 칸이 포인터를 잡고(`ButtonWidget`), 뗀 자리가 자기 밖이면 화면이 그 자리의 칸을 찾아 옮긴다(언리얼 UMG 의 DragDropOperation 자리).
     *          길게 누르기는 화면이 누른 시간을 세어 동작 메뉴를 연다(터치 — 입력 맵의 hold 문턱).
     */
    REFLECT( Abstract )
    class SW_GF_API InventorySlotWidget : public ButtonWidget
    {
    public:
        REFLECT_BODY();

        InventorySlotWidget( InventoryScreen& screen, int32 bagSlot );
        ~InventorySlotWidget() override;

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override;

        int32 getBagSlot() const { return _bagSlot; }

    private:
        InventoryScreen& _screen; ///< 이 칸을 만든 화면(화면이 트리를 소유하므로 칸보다 오래 산다)
        int32            _bagSlot;
    };
} // namespace sw
