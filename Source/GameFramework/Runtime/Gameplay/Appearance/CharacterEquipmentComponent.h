/**
 * @file CharacterEquipmentComponent.h
 * @brief 캐릭터 장비 컴포넌트입니다. 장비 칸(`Equipment`)과 가방(`Inventory`)의 아이템 인스턴스(염색 · 피해 · 떨어져 나간 부품)를 들고, 같은 오브젝트의 외형에 장비를 빌려 줍니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Delegate/WeakRef.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Runtime/Gameplay/Inventory/EquipmentCondition.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryCommand.h"

namespace sw
{
    class AppearanceDatabase;
    struct CharacterAppearanceSpec;
    class PrefabAsset;
    struct CustomizationSchemaDef;

    /**
     * @class CharacterEquipmentComponent
     * @brief 장비 칸과 가방을 들고 같은 오브젝트의 `CharacterAppearanceComponent` 에 장비를 빌려 준다(`setEquipment`). 장비 칸의 아이템이 프리셋의 장비 구성을 덮는다.
     * @details 아이템 인스턴스의 꾸미기 값(`InventorySlot::_customization` — 염색 색 · 머티리얼 변형)은 인스턴스를 따라다닌다: 벗어 가방에 넣고 다시 껴도,
     *          같은 아이템 정의의 다른 인스턴스(염색한 것 · 기본색)와 바꿔 껴도 그 인스턴스의 값이 외형 해석을 지나 그 부품의 머티리얼 값이 된다(구운 메시 캐시 키에는 들지 않는다).
     *          언리얼 Lyra 의 장비 관리자(인벤토리 항목 인스턴스 → 장비 인스턴스 → 외형) · 유니티 게임들의 아이템 인스턴스 데이터와 같은 자리다.
     *          가방은 아이템 받기 계약(`IItemReceiver`)을 포트로 내보낸다 — 상자 · 채집 · 줍기가 이 캐릭터에게 바로 준다(조종자의 `InventoryComponent` 보다 먼저 찾힌다).
     *          상태(장비 칸 · 가방)는 게임 세이브에 실린다(`serializeState` — 아이템 칸은 `Inventory::serializeSlot` 하나로 쓴다). 시작 전에 받은 상태는 시작 때 건다.
     *          장비 칸 표는 외형 데이터의 칸 표다(`SlotCatalog::makeEquipmentLayout` — 게임플레이 칸과 외형 칸이 한 표에서 나온다).
     */
    REFLECT( Category = "Character", DisplayName = "Character Equipment", Tooltip = "Equipment slots and a bag of item instances (dye, damage) that dress the character's appearance" )
    class SW_GF_API CharacterEquipmentComponent : public Component
    {
    public:
        REFLECT_BODY();
        REFLECT_INTERFACE( IItemReceiver, _itemReceiverPort );

        static constexpr uint32 kStateTag     = FourCCUtil::make( "CEQP" );
        static constexpr uint32 kStateVersion = 2; ///< 2: 아이템 칸에 꾸미기 스키마 판(`Inventory::serializeSlot`). 판 1 은 `SW_STATE_FORMAT_UPGRADE` 가 올린다

        CharacterEquipmentComponent();
        virtual ~CharacterEquipmentComponent() override;

        void onRegister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 외형이 조립할 전개 @p spec 을 알립니다(`CharacterAppearanceComponent` 가 조립 앞에서 부른다). 몸 종류 · 체형 · 프리셋 태그가 바뀌었으면 장착 조건을 다시 봅니다. */
        void setCharacterSpec( const CharacterAppearanceSpec& spec );

        /** @brief 가방에 아이템 인스턴스를 넣습니다. 가방이 없거나(시작 전) 자리가 없으면 false 입니다. */
        [[nodiscard]] bool addToBag( const InventorySlot& item );
        /** @brief 가방 칸 @p bagSlot 의 인스턴스를 장비 칸 @p slot 에 낍니다(비우면 받는 칸을 고른다). 벗겨진 것은 가방으로 간다. */
        EquipResult equipFromBag( int32 bagSlot, const hashed_string& slot );
        /** @brief 장비 칸을 벗겨 가방에 넣습니다(함께 벗겨진 것도). 가방에 자리가 없거나 거부되면 그대로 두고 false 입니다. */
        [[nodiscard]] FUNCTION( Category = "Equipment", Exec ) bool unequipToBag( const hashed_string& slot );
        /**
         * @brief 낀 아이템 인스턴스의 꾸미기 값 하나를 바꿉니다(염색). 값의 뜻은 그 아이템 외형의 스키마가 정한다.
         * @details 인스턴스의 값이 옛 판(옛 세이브)이면 스키마로 지금 판까지 올린 뒤 넣는다(`CustomizationSchemaDef::applyValue`). 칸이 비었거나, 아이템에 스키마가 없거나,
         *          스키마가 모르는 매개변수이거나, 값을 올리지 못하면(더 새 판) false 이고 인스턴스는 그대로입니다.
         */
        [[nodiscard]] bool setEquippedValue( const hashed_string& slot, const CustomizationValue& value );
        /** @brief 아이템 @p itemID 의 외형이 쓰는 꾸미기 스키마입니다(아이템 → 외형 → 스키마). 어느 하나라도 없으면 nullptr 입니다. */
        static const CustomizationSchemaDef* findItemSchema( const AppearanceDatabase& database, const hashed_string& itemID );
        /**
         * @brief 세이브에서 읽은 아이템 인스턴스의 꾸미기 값을 그 아이템 스키마의 지금 판으로 올립니다 — 세이브 · 판 올림 길의 입구다(`setEquippedValue` 의 모르는 매개변수 거절을 거치지 않는다).
         * @details 지금 판에 뜻이 없는 값은 그 값만 빼고 오류 로그를 남긴다(나머지는 읽는다). 더 새 판(다운그레이드)이면 알리고 false 다 — 부르는 쪽은 읽기를 거절하고 세이브는 그대로다.
         */
        [[nodiscard]] static bool upgradeSavedItem( const AppearanceDatabase& database, InventorySlot& inoutItem );

        /**
         * @brief 가방 · 장비 명령 하나를 검사하고 적용합니다(인벤토리 화면 · 패드 · 서버가 받은 요청 모두 이 길). 장비가 바뀌면 외형을 다시 빌려 줍니다.
         * @details 버리기가 꺼낸 조각은 @p outListDropped 뒤에 붙는다 — 월드에 놓는 일(`PickupComponent::spawn`)은 부르는 쪽이다. 시작 전이면 `InvalidSlot` 입니다.
         */
        [[nodiscard]] InventoryCommandResult applyCommand( const InventoryCommand& command, vector<InventorySlot>& outListDropped );

        const Equipment& getEquipment() const { return _equipment; }
        const Inventory& getBag() const { return _bag; }
        /** @brief 가방을 거래(가게 사기 · 팔기 — `LocalShopPurchaseProvider`)에 빌려 줍니다. 칸 조작은 `applyCommand` 를 거칩니다. */
        Inventory& getBagForTrade() { return _bag; }
        /** @brief 버린 조각을 세울 프리팹입니다(`PickupComponent::spawn`). 비면 버린 조각을 세우지 못한다 — 버리기 화면이 가방에 돌려준다. */
        const string& getPickupPrefab() const { return _pickupPrefab.path(); }
        bool          isStarted() const { return _bStarted == SW_TRUE; }
        /** @brief 화면(엔진 UI 소유 — 컴포넌트보다 오래 살 수 있다)이 이 컴포넌트를 약하게 빌리는 출처입니다(`WeakRef`). */
        WeakRefSource& getWeakRefSource() { return _weakRefSource; }

        /** @brief `ComponentStateCodec` 계약 — 장비 칸(칸 이름 + 인스턴스)과 가방입니다. 읽기는 아이템 카탈로그가 선 뒤(`onBeginPlay`)에만 된다. */
        void serializeState( StateIO& io );
        /** @brief 저장한 상태를 받습니다. 시작 전이면 보관했다가 `onBeginPlay` 가 겁니다. 읽지 못하면 알리고 지금 상태를 그대로 둡니다. */
        void restoreState( vector<uint8>&& bytes );
        /** @brief 시작 전에 받아 보관한 상태입니다(`ComponentStateCodec` 계약 — 있으면 그것이 곧 상태다). */
        const vector<uint8>& getPendingStateBytes() const { return _pendingStateBytes; }

    private:
        /** @brief 오브젝트의 태그가 바뀌었습니다(`GameObjectEvents::_tagsChanged`). */
        void handleTagsChanged( const TagContainer& tags );
        /** @brief 조건이 볼 캐릭터 정보를 오브젝트 태그와 외형 전개에서 다시 모읍니다. */
        void rebuildCharacter( const TagContainer& ownerTags );
        /** @brief 외형에 장비를 다시 빌려 줍니다 — 장비의 판이 바뀌었으면 다시 해석한다. */
        void updateAppearance();
        /** @brief 보관한 상태를 겁니다. 건 것이 없으면 false 입니다. */
        bool applyPendingState();

    private:
        EquipmentCharacterContext _character; ///< 장착 조건이 보는 캐릭터 정보(장비가 빌린다 — 장비보다 먼저 선언)
        TagContainer              _specTags;  ///< 외형 전개(프리셋)가 준 태그. 오브젝트 태그와 합쳐 `_character` 가 된다
        Subscription              _tagSubscription;
        Equipment                 _equipment;
        Inventory                 _bag;
        vector<uint8>             _pendingStateBytes; ///< 시작 전에 받은 상태(`restoreState`)
        PROPERTY( Category = "Equipment", DisplayName = "Bag Slots", Min = 1, Max = 256, Tooltip = "Item instances the character carries besides what it wears" )
        int32 _bagSlotCount;
        PROPERTY( Category = "Equipment", DisplayName = "Bag Max Weight", Min = 0.0, Tooltip = "Most weight the bag carries (0: no limit)" )
        float32 _bagMaxWeight;
        PROPERTY( Category = "Equipment", DisplayName = "Pickup Prefab", Tooltip = "Prefab a dropped item is spawned as (needs a PickupComponent; give it an interaction so it can be picked up again)" )
        AssetRef<PrefabAsset> _pickupPrefab;
        uint8                 _bStarted;
        /** @brief 아이템 받기 계약을 가방으로 넘기는 포트입니다(`REFLECT_INTERFACE`). 가방이 열리기 전이면 자리가 없다고 답한다. */
        struct ItemReceiverPort final : IItemReceiver
        {
            [[nodiscard]] ItemGrantResult receiveItem( const hashed_string& item, int32 count ) override;
            [[nodiscard]] ItemGrantResult receiveStack( const InventorySlot& stack ) override;
        };
        ItemReceiverPort _itemReceiverPort;
        // 컴포넌트 핸들(`TComponentHandle`)은 오브젝트 관리자마다의 id 공간이라 관리자가 새로 서면 같은 id 가 다른 컴포넌트를 가리킬 수 있다 — 관리자보다 오래 사는 빌림은 약한 참조다.
        WeakRefSource _weakRefSource; ///< 맨 끝 멤버 — 먼저 닫혀, 다른 멤버가 사라지는 동안 화면이 컴포넌트를 돌려받지 않는다
    };
} // namespace sw
