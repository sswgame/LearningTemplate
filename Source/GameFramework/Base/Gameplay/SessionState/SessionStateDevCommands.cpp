/**
 * @file SessionStateDevCommands.cpp
 * @brief 판 상태(`SessionStateComponent`)의 치트 개발 명령(`cheat.give` · `cheat.money`)입니다. Shipping 에는 없습니다.
 * @details 대상은 활성 씬에서 열린(`isInitialized`) 첫 판 상태입니다 — 판 상태는 게임마다 하나다. 명령은 게임 스레드에서 돈다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    namespace
    {
        struct SessionStateDevCommandsInternal
        {
            /** @brief 활성 씬에서 열린 첫 판 상태입니다. 없으면 답을 채우고 nullptr 입니다. */
            static SessionStateComponent* findSessionState( string& outReply )
            {
                GameObjectManager* pManager = GameInstance::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return nullptr;
                }
                SessionStateComponent* pFound = nullptr;
                pManager->forEachComponentOfType<SessionStateComponent>( [&pFound]( SessionStateComponent* pComponent )
                {
                    if ( pFound == nullptr && pComponent->isInitialized() )
                        pFound = pComponent;
                } );
                if ( pFound == nullptr )
                    outReply = "cheat: no initialized SessionStateComponent in the active scene";
                return pFound;
            }

            /** @brief `cheat.give <itemID> [count]` — 판 상태의 가방에 아이템을 넣습니다(슬롯 · 무게가 모자라면 일부만). */
            static bool runGive( const vector<string>& listArgument, string& outReply )
            {
                int32      count                  = 1;
                const bool bInvalidCountArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && StringUtil::parseInt( listArgument[1], count ) == false ) || count < 1;
                if ( bInvalidCountArguments )
                    return false;
                SessionStateComponent* pState = findSessionState( outReply );
                if ( pState == nullptr )
                    return true;
                Inventory& inventory = pState->getInventory();
                if ( inventory.getSlotCount() == 0 )
                {
                    outReply = "cheat.give: the session state has no inventory (slot count 0)";
                    return true;
                }
                const int32 addedCount = inventory.addItem( hashed_string( listArgument[0] ), count );
                outReply               = formatToString( "cheat.give added %# of %# '%#'", addedCount, count, listArgument[0] );
                return true;
            }

            /** @brief `cheat.money <amount> [currency]` — 지갑에 돈을 더합니다(양수만). 통화를 적지 않으면 기본 통화입니다. */
            static bool runMoney( const vector<string>& listArgument, string& outReply )
            {
                int64      amount                  = 0;
                const bool bInvalidAmountArguments = listArgument.empty() || listArgument.size() > 2 || StringUtil::parseInt64( listArgument[0], amount ) == false || amount <= 0;
                if ( bInvalidAmountArguments )
                    return false;
                SessionStateComponent* pState = findSessionState( outReply );
                if ( pState == nullptr )
                    return true;
                const hashed_string currency = listArgument.size() == 2 ? hashed_string( listArgument[1] ) : Wallet::getDefaultCurrency();
                Wallet&             wallet   = pState->getWallet();
                wallet.add( currency, amount );
                outReply = formatToString( "cheat.money balance %#%#", wallet.getBalance( currency ), ( listArgument.size() == 2 ? " " + listArgument[1] : string( " (default currency)" ) ) );
                return true;
            }
        };
    } // namespace
} // namespace sw
