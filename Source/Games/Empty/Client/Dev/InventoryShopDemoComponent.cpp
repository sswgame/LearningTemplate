#include "pch.h"

#include "Games/Empty/Client/Dev/InventoryShopDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/UI/Document/UIDocument.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Client/Inventory/InventoryScreen.h"
#include "GameFramework/Client/Shop/ShopScreen.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterEquipmentComponent.h"

namespace sw
{
    namespace
    {
        struct InventoryShopDemoInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "IVSD" );
            static constexpr uint32 kStateVersion = 1;

            /** @brief 활성 씬에서 이름 @p objectName 의 시연 컴포넌트입니다. 없으면 까닭을 @p outReply 에 적고 nullptr 입니다. */
            static InventoryShopDemoComponent* findDemo( const string& objectName, string& outReply )
            {
                GameObject*                 pObject = engine::findActiveObject( hashed_string( objectName ) );
                InventoryShopDemoComponent* pDemo   = pObject != nullptr ? pObject->getComponent<InventoryShopDemoComponent>() : nullptr;
                if ( pDemo == nullptr || pDemo->isStarted() == false )
                    outReply = "no object named '" + objectName + "' with a started InventoryShopDemoComponent";
                return pDemo != nullptr && pDemo->isStarted() ? pDemo : nullptr;
            }

            /** @brief `demo.inventory <demo>` — 인벤토리 화면을 연다. */
            static bool runOpenInventory( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                InventoryShopDemoComponent* pDemo = findDemo( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                outReply = pDemo->openInventory() ? "inventory opened" : "inventory not opened (already open or no character)";
                return true;
            }

            /** @brief `demo.shop <demo>` — 상점 화면을 연다. */
            static bool runOpenShop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                InventoryShopDemoComponent* pDemo = findDemo( listArgument[0], outReply );
                if ( pDemo == nullptr )
                    return false;
                outReply = pDemo->openShop() ? "shop opened" : "shop not opened (already open or no character)";
                return true;
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

    SW_DEV_COMMAND( DemoInventory, "demo.inventory", "demo.inventory <demo object>", "Open the runtime inventory screen of the inventory shop demo", &InventoryShopDemoInternal::runOpenInventory );
    SW_DEV_COMMAND( DemoShop, "demo.shop", "demo.shop <demo object>", "Open the runtime shop screen of the inventory shop demo", &InventoryShopDemoInternal::runOpenShop );
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
        , _wallet{}
        , _currency{}
        , _provider{}
        , _inventoryScreen{ kInvalidUIScreenHandle }
        , _shopScreen{ kInvalidUIScreenHandle }
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
        UISystem* pUI = game::findSubsystem<UISystem>();
        if ( pUI != nullptr )
        {
            pUI->closeScreen( _inventoryScreen );
            pUI->closeScreen( _shopScreen );
        }
        _inventoryScreen = kInvalidUIScreenHandle;
        _shopScreen      = kInvalidUIScreenHandle;
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
        _shop.initialize( _pShops.get(), pItems );
        _currency = _shop.getCurrency( _shopID );
        _wallet.clear();
        if ( _startingGold > 0 )
            _wallet.add( _currency, _startingGold ); // 새 판의 돈 — 세이브를 불러오면 복원이 덮는다
        return true;
    }

    void InventoryShopDemoComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void InventoryShopDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)manager;
        (void)bRespawnViews;
    }

    void InventoryShopDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( InventoryShopDemoInternal::kStateTag, InventoryShopDemoInternal::kStateVersion );
        if ( io.isWriting() )
        {
            io.value( _wallet );
            io.value( _shop );
            return;
        }
        // 지역 사본에 읽고 끝까지 맞으면 바꾼다.
        Wallet      wallet = _wallet;
        ShopManager shop   = _shop;
        io.value( wallet );
        io.value( shop );
        if ( io.isOk() == false )
            return;
        _wallet = std::move( wallet );
        _shop   = std::move( shop );
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
        if ( pUI == nullptr || pCharacter == nullptr || pUI->findScreen( _inventoryScreen ) != nullptr )
            return false;
        _inventoryScreen = InventoryScreen::open( *pUI, _inventoryDocument.path(), *pCharacter );
        return _inventoryScreen != kInvalidUIScreenHandle;
    }

    bool InventoryShopDemoComponent::openShop()
    {
        UISystem*                    pUI        = game::findSubsystem<UISystem>();
        CharacterEquipmentComponent* pCharacter = findCharacter();
        if ( pUI == nullptr || pCharacter == nullptr || isStarted() == false || pUI->findScreen( _shopScreen ) != nullptr )
            return false;
        // 가방은 캐릭터의 것이다 — 산 것은 인벤토리 화면에 그대로 보인다(칸 · 무게가 모자라면 가게가 NoRoom 으로 거절한다).
        _provider   = make_unique<LocalShopPurchaseProvider>( *_pShops, *pCharacter->getBag().getCatalog(), _shop, _wallet, pCharacter->getBagForTrade(), _shopID );
        _shopScreen = ShopScreen::open( *pUI, _shopDocument.path(), *_provider );
        return _shopScreen != kInvalidUIScreenHandle;
    }
} // namespace sw
