/**
 * @file CharacterEquipmentComponent.h
 * @brief 캐릭터 장비 컴포넌트입니다. 장비 칸(`Equipment`)과 가방(`Inventory`)의 아이템 인스턴스(염색 · 피해 · 떨어져 나간 부품)를 들고, 같은 오브젝트의 외형에 장비를 빌려 줍니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    /**
     * @class CharacterEquipmentComponent
     * @brief 장비 칸과 가방을 들고 같은 오브젝트의 `CharacterAppearanceComponent` 에 장비를 빌려 준다(`setEquipment`). 장비 칸의 아이템이 프리셋의 장비 구성을 덮는다.
     * @details 아이템 인스턴스의 꾸미기 값(`InventorySlot::_customization` — 염색 색 · 머티리얼 변형)은 인스턴스를 따라다닌다: 벗어 가방에 넣고 다시 껴도,
     *          같은 아이템 정의의 다른 인스턴스(염색한 것 · 기본색)와 바꿔 껴도 그 인스턴스의 값이 외형 해석을 지나 그 부품의 머티리얼 값이 된다(구운 메시 캐시 키에는 들지 않는다).
     *          언리얼 Lyra 의 장비 관리자(인벤토리 항목 인스턴스 → 장비 인스턴스 → 외형) · 유니티 게임들의 아이템 인스턴스 데이터와 같은 자리다.
     *          상태(장비 칸 · 가방)는 게임 세이브에 실린다(`serializeState` — 아이템 칸은 `Inventory::serializeSlot` 하나로 쓴다). 시작 전에 받은 상태는 시작 때 건다.
     *          장비 칸 표는 외형 데이터의 칸 표다(`SlotCatalog::makeEquipmentLayout` — 게임플레이 칸과 외형 칸이 한 표에서 나온다).
     */
    REFLECT( Category = "Character", DisplayName = "Character Equipment", Tooltip = "Equipment slots and a bag of item instances (dye, damage) that dress the character's appearance" )
    class SW_GF_API CharacterEquipmentComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "CEQP" );
        static constexpr uint32 kStateVersion = 1;

        CharacterEquipmentComponent();
        virtual ~CharacterEquipmentComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 가방에 아이템 인스턴스를 넣습니다. 가방이 없거나(시작 전) 자리가 없으면 false 입니다. */
        [[nodiscard]] bool addToBag( const InventorySlot& item );
        /** @brief 가방 칸 @p bagSlot 의 인스턴스를 장비 칸 @p slot 에 낍니다(비우면 받는 칸을 고른다). 벗겨진 것은 가방으로 간다. */
        EquipResult equipFromBag( int32 bagSlot, const hashed_string& slot );
        /** @brief 장비 칸을 벗겨 가방에 넣습니다(함께 벗겨진 것도). 가방에 자리가 없거나 거부되면 그대로 두고 false 입니다. */
        [[nodiscard]] bool unequipToBag( const hashed_string& slot );
        /** @brief 낀 아이템 인스턴스의 꾸미기 값 하나를 바꿉니다(염색). 칸이 비었으면 false 입니다. 값의 뜻은 그 아이템 외형의 스키마가 정한다. */
        [[nodiscard]] bool setEquippedValue( const hashed_string& slot, const CustomizationValue& value );

        const Equipment& getEquipment() const { return _equipment; }
        const Inventory& getBag() const { return _bag; }
        bool             isStarted() const { return _bStarted == SW_TRUE; }

        /** @brief `ComponentStateStore` 계약 — 장비 칸(칸 이름 + 인스턴스)과 가방입니다. 읽기는 아이템 카탈로그가 선 뒤(`onBeginPlay`)에만 된다. */
        void serializeState( StateIO& io );
        /** @brief 저장한 상태를 받습니다. 시작 전이면 보관했다가 `onBeginPlay` 가 겁니다. 읽지 못하면 알리고 지금 상태를 그대로 둡니다. */
        void restoreState( vector<uint8>&& bytes );

    private:
        /** @brief 외형에 장비를 다시 빌려 줍니다 — 장비의 판이 바뀌었으면 다시 해석한다. */
        void updateAppearance();
        /** @brief 보관한 상태를 겁니다. 건 것이 없으면 false 입니다. */
        bool applyPendingState();

    private:
        Equipment     _equipment;
        Inventory     _bag;
        vector<uint8> _pendingStateBytes; ///< 시작 전에 받은 상태(`restoreState`)
        PROPERTY( Category = "Equipment", DisplayName = "Bag Slots", Min = 1, Max = 256, Tooltip = "Item instances the character carries besides what it wears" )
        int32 _bagSlotCount;
        uint8 _bStarted;
    };
} // namespace sw
