/**
 * @file PlayerStateDevTools.cpp
 * @brief 플레이어 상태 컴포넌트의 개발 도구입니다 — 치트 `cheat.give`(가방) · `cheat.money`(지갑), 디버그 HUD 섹션 `quest`(퀘스트 일지) · `progress`(진행 조건 디버거: 월드 플래그 · 지갑 · 가방).
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
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    namespace
    {
        struct PlayerStateDevToolsInternal
        {
            static constexpr int32 kQuestHUDOrder    = 200; ///< HUD 섹션 순서 — 게임플레이 섹션 무리
            static constexpr int32 kProgressHUDOrder = 210;

            /** @brief 활성 씬의 열린 @p T(씬에 하나)입니다. 없으면 답을 채우고 nullptr 입니다. */
            template <typename T>
            static T* findInActiveScene( string& outReply )
            {
                SceneManager* const      pSceneManager = game::findSubsystem<SceneManager>();
                const GameObjectManager* pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return nullptr;
                }
                T* const pFound = PlayerComponentUtil::findOpen<T>( pManager );
                if ( pFound == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "cheat: no single open %# in the active scene", T::StaticType()->_name.c_str() );
                    outReply = string( replyBuilder.view() );
                }
                return pFound;
            }

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
            static const T* findDebugTarget( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const pManager   = GameplayDebugTarget::findActiveObjectManager();
                const T*                       pComponent = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<T>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pComponent == nullptr || pComponent->isOpen() == false )
                {
                    writer.addLineFormat( "target", "none (no open %# in the active scene)", T::StaticType()->_name.c_str() );
                    return nullptr;
                }
                writer.addLine( "target", pComponent->getOwner() != nullptr ? string( pComponent->getOwner()->getName().c_str() ) : string( "?" ) );
                return pComponent;
            }

            /** @brief 진행 중 퀘스트의 단계 · 목표 달성도, 받지 않은 퀘스트의 잠금 사유입니다(레벨 조건은 판정하지 않고 표시만 — 일지는 레벨을 모른다). */
            static void writeQuest( DebugHUDSectionWriter& writer )
            {
                if ( const QuestLogComponent* pQuestLog = findDebugTarget<QuestLogComponent>( writer ) )
                    pQuestLog->getQuestLog().writeDebugLines( writer, MathUtil::kMaxInt32, GameplayDebugTarget::kMaxLines );
            }

            /**
             * @brief 진행 조건 디버거 — 월드 플래그(없으면 건너뜀) · 지갑 · 같은 오브젝트의 가방(없으면 건너뜀, 빈 칸 제외) 순이고 줄 수 한도에서 끊는다. 읽기만 한다.
             * @details 퀘스트의 "왜 못 받나" 는 섹션 `quest` 가 적는다. 카탈로그 정의값이 아니라 이 판의 값이다.
             */
            static void writeProgress( DebugHUDSectionWriter& writer )
            {
                const WalletComponent* const pWallet = findDebugTarget<WalletComponent>( writer );
                if ( pWallet == nullptr )
                    return;
                const GameStateComponent* const pWorld    = GameStateComponent::findOpen( GameplayDebugTarget::findActiveObjectManager() );
                const InventoryComponent* const pBag      = PlayerComponentUtil::findOnOwner<InventoryComponent>( *pWallet );
                const uint32                    maxLines  = GameplayDebugTarget::kMaxLines;
                uint32                          lineCount = 0;
                uint32                          skipped   = 0;
                const auto                      add       = [&]( string_view label, string_view value )
                {
                    if ( lineCount >= maxLines )
                    {
                        ++skipped;
                        return;
                    }
                    writer.addLine( label, value );
                    ++lineCount;
                };
                if ( pWorld != nullptr )
                {
                    vector<GameFlagEntry> listFlag;
                    pWorld->getFlags().fillEntries( listFlag );
                    for ( const GameFlagEntry& flag : listFlag )
                    {
                        fixed_string<constant::buffer::kMax256> label;
                        formatstring( label.data(), label.capacity(), "flag %#", flag._name.c_str() );
                        fixed_string<constant::buffer::kMax32> value;
                        formatstring( value.data(), value.capacity(), "%#", flag._value );
                        add( label.view(), value.view() );
                    }
                }
                for ( const WalletBalance& balance : pWallet->getWallet().getBalances() )
                {
                    fixed_string<constant::buffer::kMax256> label;
                    formatstring( label.data(), label.capacity(), "wallet %#", balance._currency.c_str() );
                    fixed_string<constant::buffer::kMax32> value;
                    formatstring( value.data(), value.capacity(), "%#", balance._amount );
                    add( label.view(), value.view() );
                }
                if ( pBag != nullptr )
                {
                    const Inventory& bag = pBag->getInventory();
                    for ( int32 slot = 0; slot < bag.getSlotCount(); ++slot )
                    {
                        const InventorySlot& content = bag.getSlot( slot );
                        if ( content.isEmpty() )
                            continue;
                        fixed_string<constant::buffer::kMax32> label;
                        formatstring( label.data(), label.capacity(), "slot %#", slot );
                        fixed_string<constant::buffer::kMax256> value;
                        formatstring( value.data(), value.capacity(), "%# x%#", content._itemID.c_str(), content._count );
                        add( label.view(), value.view() );
                    }
                }
                if ( skipped > 0 )
                    writer.addLineFormat( "...", "%# more", skipped );
            }
        };
    } // namespace

    // 등록 줄은 기능 파일로 옮길 때(165e16647c) 빠졌다 — 함수만 남아 명령이 없었다.
    SW_DEV_COMMAND( CheatGive, "cheat.give", "cheat.give <itemID> [count]", "Add items to the player's inventory (partly when slots or weight run out)", &PlayerStateDevToolsInternal::runGive );
    SW_DEV_COMMAND( CheatMoney, "cheat.money", "cheat.money <amount> [currency]", "Add money to the player's wallet (default currency when none is given)", &PlayerStateDevToolsInternal::runMoney );
    SW_DEBUG_HUD_SECTION( quest, "quest", "Quests", PlayerStateDevToolsInternal::kQuestHUDOrder, DebugHUDSectionFlag::kNone, &PlayerStateDevToolsInternal::writeQuest );
    SW_DEBUG_HUD_SECTION( progress, "progress", "Progress (flags, wallet, bag)", PlayerStateDevToolsInternal::kProgressHUDOrder, DebugHUDSectionFlag::kNone, &PlayerStateDevToolsInternal::writeProgress );
} // namespace sw
