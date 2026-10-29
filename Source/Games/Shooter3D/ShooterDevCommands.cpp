/**
 * @file ShooterDevCommands.cpp
 * @brief Shooter3D 의 치트 개발 명령(`cheat.ammo` · `cheat.shooter.heal` · `cheat.shooter.god`)입니다. Shipping 에는 없습니다.
 * @details 대상은 활성 씬의 모든 `ShooterPlayerComponent` 입니다. 보상과 같은 길(`addWaveAmmo` · `restoreHealth`)을 쓰므로 게임 스레드에서만 부른다.
 *          체력 명령 이름이 `cheat.heal`(어빌리티 시스템)과 겹치지 않게 `cheat.shooter.heal` 이다 — 슈터 플레이어는 어빌리티 시스템이 아니라 `Vitality` 를 쓴다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterDevCommandsInternal
        {
            /** @brief 활성 씬의 슈터 플레이어를 모읍니다. 씬이 없거나 하나도 없으면 답을 채우고 false 입니다. */
            [[nodiscard]] static bool collectPlayers( vector<ShooterPlayerComponent*>& outListPlayer, string& outReply )
            {
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                GameObjectManager*  pManager      = pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "cheat: no active scene";
                    return false;
                }
                pManager->forEachComponentOfType<ShooterPlayerComponent>( [&outListPlayer]( ShooterPlayerComponent* pPlayer )
                { outListPlayer.push_back( pPlayer ); } );
                if ( outListPlayer.empty() )
                {
                    outReply = "cheat: no ShooterPlayerComponent in the active scene";
                    return false;
                }
                return true;
            }

            /** @brief `cheat.ammo` — 탄 보상과 같이 무기마다 탄창 두 개를 예비 탄에 더합니다. */
            static bool runAmmo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<ShooterPlayerComponent*> listPlayer;
                if ( collectPlayers( listPlayer, outReply ) == false )
                    return true;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    pPlayer->addWaveAmmo();
                }
                outReply = formatToString( "cheat.ammo refilled %# player(s)", listPlayer.size() );
                return true;
            }

            /** @brief `cheat.shooter.heal` — 살아 있는 슈터 플레이어의 체력을 가득 채웁니다(쓰러진 플레이어는 `Vitality` 가 회복을 받지 않는다). */
            static bool runHeal( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<ShooterPlayerComponent*> listPlayer;
                if ( collectPlayers( listPlayer, outReply ) == false )
                    return true;
                int32 healedCount = 0;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    if ( pPlayer->isAlive() == false )
                        continue;
                    pPlayer->restoreHealth( pPlayer->getMaxHealth() ); // 최대 체력만큼 채우면 가득 찬다(heal 이 최대에서 자른다).
                    ++healedCount;
                }
                outReply = formatToString( "cheat.shooter.heal healed %# of %# player(s)", healedCount, listPlayer.size() );
                return true;
            }

            /**
             * @brief `cheat.shooter.god` — 슈터 플레이어의 피해 면역을 뒤집습니다. 첫 플레이어의 지금 값을 기준으로 모두 같은 값이 됩니다.
             * @details `cheat.god`(어빌리티 시스템 태그)은 `Vitality` 를 쓰는 슈터 플레이어에 닿지 않아 따로 둔다.
             */
            static bool runGod( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                vector<ShooterPlayerComponent*> listPlayer;
                if ( collectPlayers( listPlayer, outReply ) == false )
                    return true;
                const bool bImmune = listPlayer.front()->isDamageImmune() == false;
                for ( ShooterPlayerComponent* pPlayer : listPlayer )
                {
                    pPlayer->setDamageImmune( bImmune );
                }
                outReply = formatToString( "%#%# player(s)", string( bImmune ? "cheat.shooter.god on for " : "cheat.shooter.god off for " ), listPlayer.size() );
                return true;
            }
        };
    } // namespace
} // namespace sw
