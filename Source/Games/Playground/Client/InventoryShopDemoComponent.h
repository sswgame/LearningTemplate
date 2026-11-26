/**
 * @file InventoryShopDemoComponent.h
 * @brief 인벤토리 · 오프라인 상점 시연입니다. 같은 씬의 캐릭터(장비 컴포넌트 — 가방 · 장비 칸 · 외형)에 지갑과 가게를 붙여, 엔진 런타임 UI 의 인벤토리 화면 · 상점 화면을 엽니다.
 * @details 시나리오: `game/playground/automation/inventory.scenario.xml` · `shopoffline.scenario.xml`. 지갑 · 가게 상태는 게임 세이브에 싣고(`serializeGameState`), 가방 · 장비는 장비 컴포넌트가 싣는다.
 *          허브 게임(Playground)으로 옮길 때 이 컴포넌트를 그대로 옮긴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Screen/UIOwnedScreen.h"

#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ShopPurchase.h"

namespace sw
{
    class CharacterEquipmentComponent;
    struct UIDocumentAsset;

    /**
     * @class InventoryShopDemoComponent
     * @brief 지갑 · 가게(`ShopManager`)를 들고, 캐릭터 가방 위에 오프라인 구매 공급자(`LocalShopPurchaseProvider`)를 세웁니다. 화면은 개발 명령 `exec <오브젝트> openInventory` · `openShop` 이 엽니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Inventory Shop Demo", Tooltip = "Wallet and shop over a character bag; opens the runtime inventory and shop screens" )
    class InventoryShopDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        InventoryShopDemoComponent();
        virtual ~InventoryShopDemoComponent() override;

        void onEndPlay() override;
        void serializeGameState( StateIO& io ) override;

        /** @brief 인벤토리 화면을 엽니다. 이미 열렸거나 캐릭터가 없으면 false 입니다. */
        [[nodiscard]] FUNCTION( Category = "Demo", Exec ) bool openInventory();
        /** @brief 상점 화면을 엽니다. 이미 열렸거나 판이 열리지 않았으면 false 입니다. */
        [[nodiscard]] FUNCTION( Category = "Demo", Exec ) bool openShop();

        const Wallet&        getWallet() const { return _wallet; }
        const hashed_string& getCurrency() const { return _currency; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;

    private:
        /** @brief 같은 씬의 캐릭터 장비 컴포넌트입니다(시작했을 때만). */
        CharacterEquipmentComponent* findCharacter() const;
        void                         closeScreens();

    private:
        PROPERTY( Category = "Data", DisplayName = "Shops", Tooltip = "Shop catalog (stock, prices)" )
        AssetRef<ShopCatalog> _shopsPath;
        PROPERTY( Category = "Data", DisplayName = "Shop", Tooltip = "Shop id in the catalog" )
        hashed_string _shopID;
        PROPERTY( Category = "Data", DisplayName = "Starting Gold", Min = 0, Tooltip = "Gold a new game starts with" )
        int32 _startingGold;
        PROPERTY( Category = "Scene", DisplayName = "Character", Tooltip = "Object with the CharacterEquipmentComponent whose bag the screens show" )
        hashed_string _characterName;
        PROPERTY( Category = "UI", DisplayName = "Inventory Document" )
        AssetRef<UIDocumentAsset> _inventoryDocument;
        PROPERTY( Category = "UI", DisplayName = "Shop Document" )
        AssetRef<UIDocumentAsset> _shopDocument;

        BoundAssetRef<ShopCatalog> _pShops;
        SW_CODEC_STATE ShopManager _shop;
        SW_TRANSIENT vector<ShopEvent> _listShopEvent; ///< 이번 프레임의 가게 알림(용량만 넘긴다)
        SW_CODEC_STATE Wallet          _wallet;
        SW_TRANSIENT hashed_string     _currency;
        SW_TRANSIENT unique_ptr<LocalShopPurchaseProvider> _provider;
        SW_TRANSIENT UIOwnedScreen                         _inventoryScreen;
        SW_TRANSIENT UIOwnedScreen                         _shopScreen;
    };
} // namespace sw
