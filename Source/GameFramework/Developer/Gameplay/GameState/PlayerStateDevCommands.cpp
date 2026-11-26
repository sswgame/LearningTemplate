/**
 * @file PlayerStateDevCommands.cpp
 * @brief 플레이어 상태(`PlayerStateComponent`)의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 * @details 모두 개발 도구라 컴포넌트의 공개 API 에 두지 않는다. 씬 하나를 읽으므로 열린 컴포넌트가 둘이면 `PlayerComponentUtil::findOpen` 이 오류로 멈춘다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    namespace
    {
        struct PlayerStateDevCommandsInternal
        {
            /** @brief `cheat.give <itemID> [count]` — 플레이어 가방에 아이템을 넣습니다(슬롯 · 무게가 모자라면 일부만). */
            static bool runGive( const vector<string>& listArgument, string& outReply )
            {
                int32      count                  = 1;
                const bool bInvalidCountArguments = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && StringUtil::parseInt( listArgument[1], count ) == false ) || count < 1;
                if ( bInvalidCountArguments )
                    return false;
                InventoryComponent* const pBag = findInActiveScene<InventoryComponent>( outReply );
                if ( pBag == nullptr )
                    return true;
                const int32                              addedCount = pBag->getInventory().addItem( hashed_string( listArgument[0] ), count );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "cheat.give added %# of %# '%#'", addedCount, count, listArgument[0] );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `cheat.money <amount> [currency]` — 플레이어 지갑에 돈을 더합니다(양수만). 통화를 적지 않으면 기본 통화입니다. */
            static bool runMoney( const vector<string>& listArgument, string& outReply )
            {
                int64      amount                  = 0;
                const bool bInvalidAmountArguments = listArgument.empty() || listArgument.size() > 2 || StringUtil::parseInt64( listArgument[0], amount ) == false || amount <= 0;
                if ( bInvalidAmountArguments )
                    return false;
                WalletComponent* const pWallet = findInActiveScene<WalletComponent>( outReply );
                if ( pWallet == nullptr )
                    return true;
                const hashed_string currency = listArgument.size() == 2 ? hashed_string( listArgument[1] ) : Wallet::getDefaultCurrency();
                Wallet&             wallet   = pWallet->getWallet();
                wallet.add( currency, amount );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                if ( listArgument.size() == 2 )
                    replyBuilder.appendFormat( "cheat.money balance %# %#", wallet.getBalance( currency ), listArgument[1] );
                else
                    replyBuilder.appendFormat( "cheat.money balance %# (default currency)", wallet.getBalance( currency ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief 디버그 섹션의 대상(`debug.target`, 없으면 오브젝트 id 가 가장 낮은 것)의 열린 @p T 입니다. 없으면 그렇게 적고 nullptr 입니다. */
            template <typename T>
        };
    } // namespace

    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatGive, "cheat.give", "cheat.give <itemID> [count]", "Add items to the player's inventory (partly when slots or weight run out)", &PlayerStateDevCommandsInternal::runGive );
    SW_DEV_COMMAND( CheatMoney, "cheat.money", "cheat.money <amount> [currency]", "Add money to the player's wallet (default currency when none is given)", &PlayerStateDevCommandsInternal::runMoney );
} // namespace sw
