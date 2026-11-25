/**
 * @file Games/Playground/Online/Client/OnlineShop.cpp
 * @brief Playground 의 온라인 상점 화면을 여는 개발 명령(`playground.online.shop`)입니다. 화면은 공용 상점 화면(`ShopScreen`), 공급자는 흐름이 든 경제 키트 공급자(`EconomyShopPurchaseProvider`)다.
 * @details 클라이언트 전용 소스다(Client/ — 전용 서버는 짓지 않는다). 같은 화면을 오프라인 시연(Empty `InventoryShopDemoComponent`)은 가게 공급자로 연다 — 공급자만 갈아 끼운다.
 *          시나리오: `game/playground/automation/shooter3d_onlineshop.scenario.xml`.
 */
#include "pch.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Client/Shop/ShopScreen.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/EconomyShopPurchaseProvider.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

namespace sw
{
    namespace
    {
        struct PlaygroundOnlineShopInternal
        {
            static constexpr const utf8* kShopDocument = "common/ui/shop/shop.ui.xml"; ///< 공용 상점 견본 문서

            /** @brief `playground.online.shop` — 세션의 경제 클라이언트로 상점 화면을 연다. 로그인 뒤에 쓴다(구매는 세션 요청이다). */
            static bool runOpenShop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                OnlineSession*        pSession = game::findSubsystem<OnlineSession>();
                PlaygroundOnlineFlow* pFlow    = game::findSubsystem<PlaygroundOnlineFlow>();
                UISystem*             pUI      = game::findSubsystem<UISystem>();
                if ( pSession == nullptr || pFlow == nullptr || pUI == nullptr )
                {
                    outReply = "playground.online.shop: no Playground game instance with UI";
                    return true;
                }
                EconomyShopPurchaseProvider* pProvider = pFlow->findShopProvider( *pSession );
                if ( pProvider == nullptr )
                {
                    outReply = "playground.online.shop: no offer catalog";
                    return true;
                }
                const UIScreenHandle handle = ShopScreen::open( *pUI, kShopDocument, *pProvider );
                outReply                    = handle != kInvalidUIScreenHandle ? "playground.online.shop: opened" : "playground.online.shop: the shop document did not open";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( PlaygroundOnlineShop, "playground.online.shop", "playground.online.shop", "Open the online shop screen (economy kit purchases through the session)", &PlaygroundOnlineShopInternal::runOpenShop );
} // namespace sw
