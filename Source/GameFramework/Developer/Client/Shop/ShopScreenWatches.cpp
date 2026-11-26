/**
 * @file ShopScreenWatches.cpp
 * @brief `ShopScreen` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Client/Shop/ShopScreen.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ShopScreenDevInternal
        {
            /** @brief 맨 위의 상점 화면입니다(닫는 중 제외). 없으면 @p outValue 에 까닭을 적고 nullptr 입니다. */
            static const ShopScreen* findTopShop( AutomationWatchValue& outValue )
            {
                const UISystem* pUI = game::findSubsystem<UISystem>();
                if ( pUI != nullptr )
                {
                    const vector<unique_ptr<UIScreen>>& listScreen = pUI->getScreens();
                    for ( auto iter = listScreen.rbegin(); iter != listScreen.rend(); ++iter )
                    {
                        const ShopScreen* pShop = castTo<ShopScreen>( iter->get() );
                        if ( pShop != nullptr && pShop->isClosing() == false )
                            return pShop;
                    }
                }
                outValue.setText( "no shop screen is open" );
                return nullptr;
            }

            /** @brief 감시 값 `Shop.PurchaseCount` 입니다. 열린 상점 화면에서 끝난 구매 수(성공 · 실패 모두)입니다. */
            [[nodiscard]] static bool readPurchaseCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ShopScreen* pShop = findTopShop( outValue );
                if ( pShop == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pShop->getFinishedCount() ) );
                return true;
            }

            /** @brief 감시 값 `Shop.LastStatus` 입니다. 마지막 구매 결과 이름(`ShopPurchaseStatus`)이고, 아직 없으면 `none` 입니다. */
            [[nodiscard]] static bool readLastStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ShopScreen* pShop = findTopShop( outValue );
                if ( pShop == nullptr )
                    return false;
                outValue.setText( pShop->getFinishedCount() == 0 ? "none" : getEnumName( pShop->getLastReceipt()._status ) );
                return true;
            }

            /** @brief 감시 값 `Shop.Busy` 입니다. 구매 확인 · 답을 기다리는 동안 1 입니다. */
            [[nodiscard]] static bool readBusy( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                (void)query;
                const ShopScreen* pShop = findTopShop( outValue );
                if ( pShop == nullptr )
                    return false;
                outValue.setNumber( pShop->isBusy() ? 1.0 : 0.0 );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( shopPurchaseCount, "Shop.PurchaseCount", "Finished purchases (succeeded or failed) of the open shop screen", &ShopScreenDevInternal::readPurchaseCount, false, false );
    SW_AUTOMATION_QUERY_WATCH( shopLastStatus, "Shop.LastStatus", "Status name of the last purchase of the open shop screen (ShopPurchaseStatus); none before the first", &ShopScreenDevInternal::readLastStatus,
                               false, false );
    SW_AUTOMATION_QUERY_WATCH( shopBusy, "Shop.Busy", "1 while the open shop screen waits for a purchase confirmation or answer", &ShopScreenDevInternal::readBusy, false, false );
} // namespace sw
