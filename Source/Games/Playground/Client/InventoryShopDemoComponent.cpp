#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/Screen/UIOwnedScreen.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Client/Inventory/InventoryScreen.h"
#include "GameFramework/Client/Shop/ShopScreen.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"
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
        void               updateGame( GameSeconds frameTime ) override;

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

    namespace
    {
        struct InventoryShopDemoInternal
        {
            static constexpr FormatDef kStateFormat{ "IVSD" };

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventBought    = "ShopBought";
            static constexpr const utf8* kEventSold      = "ShopSold";
            static constexpr const utf8* kEventRestocked = "ShopRestocked";

            /** @brief 가게 알림 @p kind 에 낼 소리입니다. */
            static const utf8* findSound( ShopEvent::Kind kind )
            {
                switch ( kind )
                {
                    case ShopEvent::Kind::Bought:
                    case ShopEvent::Kind::BoughtBack:
                    {
                        return kEventBought;
                    }
                    case ShopEvent::Kind::Sold:
                    {
                        return kEventSold;
                    }
                    case ShopEvent::Kind::Restocked:
                    {
                        return kEventRestocked;
                    }
                }
                return nullptr;
            }

            /** @brief 감시 값 `Demo.Gold` — 시연 지갑의 가게 통화 잔액입니다. */
            [[nodiscard]] static bool readGold( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const InventoryShopDemoComponent* pDemo = query.findTargetComponent<InventoryShopDemoComponent>( "InventoryShopDemoComponent", outValue );
                if ( pDemo == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pDemo->getWallet().getBalance( pDemo->getCurrency() ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( demoGold, "Demo.Gold", "Shop currency balance of the inventory shop demo wallet", &InventoryShopDemoInternal::readGold, true, false );

    InventoryShopDemoComponent::InventoryShopDemoComponent()
        : GameModeComponent{}
        , _shopsPath{}
        , _shopID{}
        , _startingGold{ 0 }
        , _characterName{}
        , _inventoryDocument{}
        , _shopDocument{}
        , _pShops{}
        , _shop{}
        , _listShopEvent{}
        , _wallet{}
        , _currency{}
        , _provider{}
        , _inventoryScreen{}
        , _shopScreen{}
    {
    }

    InventoryShopDemoComponent::~InventoryShopDemoComponent()
    {
        closeScreens();
    }

    void InventoryShopDemoComponent::onEndPlay()
    {
        closeScreens(); // 화면은 캐릭터 · 공급자를 빌려 쓴다 — 먼저 닫는다
        _provider.reset();
        GameModeComponent::onEndPlay();
    }

    void InventoryShopDemoComponent::closeScreens()
    {
        _inventoryScreen.reset();
        _shopScreen.reset();
    }

    bool InventoryShopDemoComponent::startGame()
    {
        if ( acquireRequired( _shopsPath, _pShops ) == false )
            return false;
        const AppearanceDatabase* pDatabase = game::findSubsystem<AppearanceDatabase>();
        const ItemCatalog*        pItems    = pDatabase != nullptr ? pDatabase->getItemCatalog() : nullptr;
        if ( pItems == nullptr || _pShops->find( _shopID ) == nullptr )
        {
            SW_LOG_ERROR_ID( "InventoryShopDemo.NoItemsOrShop", "Inventory shop demo needs the item catalog (AppearanceDatabase) and the shop '%#' in %#", _shopID.c_str(), _shopsPath.c_str() );
            return false;
        }
        _shop.initialize( *_pShops, *pItems );
        _currency = _shop.getCurrency( _shopID );
        _wallet.clear();
        if ( _startingGold > 0 )
            _wallet.add( _currency, _startingGold ); // 새 판의 돈 — 세이브를 불러오면 복원이 덮는다
        return true;
    }

    void InventoryShopDemoComponent::updateGame( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        (void)deltaTime;
        _listShopEvent.clear();
        _shop.pollEvents( _listShopEvent );
        for ( const ShopEvent& event : _listShopEvent )
        {
            if ( const utf8* const pSound = InventoryShopDemoInternal::findSound( event._kind ) )
                getSoundQueue().queueEvent( pSound );
        }
    }

    void InventoryShopDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( InventoryShopDemoInternal::kStateFormat );
        io.value( _wallet );
        if ( io.valueOpened( _shop, [this]( ShopManager& shop )
        {
            const AppearanceDatabase* const pDatabase = game::findSubsystem<AppearanceDatabase>();
            const ItemCatalog* const        pItems    = pDatabase != nullptr ? pDatabase->getItemCatalog() : nullptr;
            if ( _pShops == nullptr || pItems == nullptr )
                return false;
            shop.initialize( *_pShops, *pItems ); // 다시 읽힌 카탈로그면 새 판으로 읽는다
            return true;
        } ) == false )
            return;
    }

    CharacterEquipmentComponent* InventoryShopDemoComponent::findCharacter() const
    {
        GameObject*                  pObject    = engine::findActiveObject( _characterName );
        CharacterEquipmentComponent* pCharacter = pObject != nullptr ? pObject->getComponent<CharacterEquipmentComponent>() : nullptr;
        return pCharacter != nullptr && pCharacter->isStarted() ? pCharacter : nullptr;
    }

    bool InventoryShopDemoComponent::openInventory()
    {
        UISystem*                    pUI        = game::findSubsystem<UISystem>();
        CharacterEquipmentComponent* pCharacter = findCharacter();
        if ( pUI == nullptr || pCharacter == nullptr || _inventoryScreen.find() != nullptr )
            return false;
        _inventoryScreen = UIOwnedScreen( *pUI, InventoryScreen::open( *pUI, _inventoryDocument.path(), *pCharacter ) );
        return _inventoryScreen.isHeld();
    }

    bool InventoryShopDemoComponent::openShop()
    {
        UISystem*                    pUI        = game::findSubsystem<UISystem>();
        CharacterEquipmentComponent* pCharacter = findCharacter();
        if ( pUI == nullptr || pCharacter == nullptr || isStarted() == false || _shopScreen.find() != nullptr )
            return false;
        // 가방은 캐릭터의 것이다 — 산 것은 인벤토리 화면에 그대로 보인다(칸 · 무게가 모자라면 가게가 NoRoom 으로 거절한다).
        _provider   = make_unique<LocalShopPurchaseProvider>( *_pShops, *pCharacter->getBag().getCatalog(), _shop, _wallet, pCharacter->getBagForTrade(), _shopID );
        _shopScreen = UIOwnedScreen( *pUI, ShopScreen::open( *pUI, _shopDocument.path(), *_provider ) );
        return _shopScreen.isHeld();
    }
} // namespace sw

#include "InventoryShopDemoComponent.cpp.gen.inl"
