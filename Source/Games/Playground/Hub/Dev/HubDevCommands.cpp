/**
 * @file HubDevCommands.cpp
 * @brief 허브 월드(`HubWorldComponent`)와 카드 탁자(`HubCardTableComponent`)의 개발 명령(`hub.*`)과 감시 값(`Hub.*`)입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"
#include "Games/Playground/Hub/HubPawnReplicationComponent.h"
#include "Games/Playground/Hub/HubWorldComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 허브 컴포넌트는 활성 씬에서 찾습니다. */
        struct HubWorldComponentDevInternal
        {
            /** @brief 허브 컴포넌트가 없을 때의 명령 답입니다. */
            static constexpr const utf8* kNoHubReply = "hub: no connected hub client in the active scene";

            /** @brief 활성 씬의 연결된 허브 컴포넌트입니다. 없으면 nullptr 이고 부르는 쪽이 `kNoHubReply` 로 답한다. */
            [[nodiscard]] static HubWorldComponent* findHub()
            {
                const SceneManager* const pScenes  = game::findSubsystem<SceneManager>();
                const Scene* const        pScene   = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const  pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                // 연결된 허브 클라이언트는 씬에 하나다 — 둘이면 오류로 명령을 보내지 않는다(등록 순서로 고르지 않는다).
                return pManager != nullptr ? pManager->getComponentRegistry().findUnique<HubWorldComponent>( []( const HubWorldComponent& hub )
                { return hub.getClient().isActive(); } )
                                           : nullptr;
            }

            [[nodiscard]] static bool parseID( const string& text, uint32& outID )
            {
                uint64 value = 0;
                if ( StringUtil::parseUint64( text, value ) == false || value > 0xFFFFFFFFull )
                    return false;
                outID = static_cast<uint32>( value );
                return true;
            }

            /** @brief 하나의 번호를 받는 행동 명령입니다. */
            static bool runIDCommand( const vector<string>& listArgument, string& outReply, const utf8* pFunctionName )
            {
                uint32 targetID = 0;
                if ( listArgument.size() != 1 || parseID( listArgument[0], targetID ) == false )
                    return false;
                HubWorldComponent* const pHub = findHub();
                if ( pHub == nullptr )
                {
                    outReply = kNoHubReply;
                    return true;
                }
                const uint32                             sequence = pHub->getClient().callServerFunction( pFunctionName, { ReflectedValue::make( targetID ) } );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "hub: %# %# sent (call %#)", pFunctionName, targetID, sequence );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runAttack( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "attackCreature" ); }
            static bool runGather( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "gatherHerb" ); }
            static bool runInvite( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "inviteToParty" ); }
            static bool runAccept( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "acceptPartyInvite" ); }

            static bool runLeave( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                HubWorldComponent* const pHub = findHub();
                if ( pHub == nullptr )
                {
                    outReply = kNoHubReply;
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "hub: leaveParty sent (call %#)", pHub->getClient().callServerFunction( "leaveParty", {} ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const HubWorldComponent* const pHub = findHub();
                if ( pHub == nullptr )
                {
                    outReply = kNoHubReply;
                    return true;
                }
                const HubAvatarAgent&                    agent = pHub->getClient().getAgent();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "hub: avatar %# level %# experience %# coins %# herbs %# defeats %# party %# visible %#", agent._avatarID, agent._shownLevel, agent._shownExperience,
                                           agent._shownCoins, agent._shownHerbCount, agent._shownDefeatCount, agent._shownPartySize, pHub->getClient().getView().getEntityCount() );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "Hub.Status", "[Hub] %#", outReply.c_str() );
                return true;
            }

            /** @brief 클라이언트가 서버의 값을 받았을 때만 값이 있습니다(받기 전에는 "값 없음" — 0 과 구분한다). */
            static const HubAvatarAgent* findShownAgent( const HubWorldComponent& hub )
            {
                const HubAvatarAgent& agent = hub.getClient().getAgent();
                return hub.getClient().isActive() && agent._avatarID != 0 ? &agent : nullptr;
            }

            [[nodiscard]] static bool readAvatarID( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_avatarID ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readLevel( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_shownLevel ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readExperience( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_shownExperience ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readCoins( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_shownCoins ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readHerbs( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_shownHerbCount ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readDefeats( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_shownDefeatCount ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readPartySize( const HubWorldComponent& hub, float64& outValue )
            {
                const HubAvatarAgent* pAgent = findShownAgent( hub );
                outValue                     = pAgent != nullptr ? static_cast<float64>( pAgent->_shownPartySize ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readResultCount( const HubWorldComponent& hub, float64& outValue )
            {
                outValue = static_cast<float64>( hub.getClient().getAgent()._resultCount );
                return hub.getClient().isActive();
            }

            [[nodiscard]] static bool readLastResult( const HubWorldComponent& hub, float64& outValue )
            {
                outValue = static_cast<float64>( static_cast<int32>( hub.getClient().getAgent()._lastResult ) );
                return hub.getClient().isActive() && hub.getClient().getAgent()._resultCount > 0;
            }

            [[nodiscard]] static bool readVisibleEntities( const HubWorldComponent& hub, float64& outValue )
            {
                outValue = static_cast<float64>( hub.getClient().getView().getEntityCount() );
                return hub.getClient().isActive();
            }

            /** @brief 첫 짐승(`HubWorld::kPlacementIDBase`)의 복제된 체력입니다. 보이지 않거나 쓰러졌으면 값이 없습니다. */
            [[nodiscard]] static bool readFirstCreatureHealth( const HubWorldComponent& hub, float64& outValue )
            {
                const MMOEntity* pEntity = hub.getClient().isActive() ? hub.getClient().getView().findEntity( HubWorld::kPlacementIDBase ) : nullptr;
                if ( pEntity == nullptr )
                    return false;
                BitReader         reader( pEntity->_listState.data(), static_cast<int32>( pEntity->_listState.size() ) );
                HubEntityNetState state;
                if ( ReplicatedLayout::readFull( reader, state, nullptr ) != FixedReadResult::Ok || state._typeID != HubEntityType::kCreature )
                    return false;
                outValue = static_cast<float64>( state._stateValue );
                return true;
            }

            /**
             * @brief 서버가 알린 공격 재사용 대기의 끝 틱을 월드의 서버 시계가 지났으면 1 입니다. 시나리오가 다음 공격을 이 값으로 기다린다.
             * @details 대기는 서버의 게임 시간이라 실제 시간으로 재면 서버가 밀리거나 멈춘 만큼 어긋난다. 클라이언트의 시계 추정은 받은 틱보다 `NetClock::kMaxLeadSeconds` 까지 앞설 수 있어 그만큼을 뺀다 —
             *          그 틱은 서버가 이미 지난 틱이다.
             */
            [[nodiscard]] static bool readAttackReady( const HubWorldComponent& hub, float64& outValue )
            {
                const NetLinkValue* const pLink = SceneNetLink::findBoundFor( hub );
                if ( hub.getClient().isActive() == false || pLink == nullptr )
                    return false;
                const HubAvatarAgent& agent     = hub.getClient().getAgent();
                const NetClock&       clock     = pLink->_pRouter->getWorldClock();
                const float64         leadTicks = static_cast<float64>( NetClock::kMaxLeadSeconds / NetClock::kTickSeconds );
                const bool            bReady    = agent._resultCount == 0 || clock.getServerTick() - leadTicks >= static_cast<float64>( agent._attackReadyTick );
                outValue                        = bReady ? 1.0 : 0.0;
                return true;
            }
        };

        /** @brief 카드 탁자 개발 명령입니다. 탁자는 활성 씬에서 찾습니다. */
        struct HubCardTableDevInternal
        {
            [[nodiscard]] static HubCardTableComponent* findTable()
            {
                const SceneManager* const pScenes  = game::findSubsystem<SceneManager>();
                const Scene* const        pScene   = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const  pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                return pManager != nullptr ? HubCardTableComponent::findClientTable( *pManager ) : nullptr;
            }

            static bool runCards( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 || ( listArgument[0] != "join" && listArgument[0] != "leave" && listArgument[0] != "play" && listArgument[0] != "draw" ) )
                    return false;
                HubCardTableComponent* const pTable = findTable();
                if ( pTable == nullptr )
                {
                    outReply = "hub.cards: no connected card table in the active scene";
                    return true;
                }
                bool bSent = false;
                if ( listArgument[0] == "join" )
                    bSent = pTable->requestSeat();
                else if ( listArgument[0] == "leave" )
                    bSent = pTable->requestLeave();
                else
                    bSent = pTable->requestPlay( listArgument[0] == "draw" );
                outReply = bSent ? "hub.cards: sent" : "hub.cards: refused - already seated, not seated, or no game yet";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( HubAttack, "hub.attack", "hub.attack <creatureID>", "Ask the hub server to strike a creature (ids start at 100000 in spawn order)", &HubWorldComponentDevInternal::runAttack );
    SW_DEV_COMMAND( HubGather, "hub.gather", "hub.gather <herbID>", "Ask the hub server to gather a herb", &HubWorldComponentDevInternal::runGather );
    SW_DEV_COMMAND( HubInvite, "hub.invite", "hub.invite <avatarID>", "Invite another avatar to this avatar's party", &HubWorldComponentDevInternal::runInvite );
    SW_DEV_COMMAND( HubAccept, "hub.accept", "hub.accept <leaderAvatarID>", "Accept the party invite of an avatar", &HubWorldComponentDevInternal::runAccept );
    SW_DEV_COMMAND( HubLeave, "hub.leave", "hub.leave", "Leave the party", &HubWorldComponentDevInternal::runLeave );
    SW_DEV_COMMAND( HubStatus, "hub.status", "hub.status", "Show the progress the hub server last sent", &HubWorldComponentDevInternal::runStatus );

    SW_AUTOMATION_GETTER_WATCH( hubRulesValid, "Hub.RulesValid", "1 when the scene's hub rules build a valid hub", HubWorldComponent, &HubWorldComponent::getRulesValid );
    SW_AUTOMATION_GETTER_WATCH( hubNetRole, "Hub.NetRole", "0 alone, 1 hub server, 2 hub client", HubWorldComponent, &HubWorldComponent::getNetRoleValue );
    SW_AUTOMATION_COMPONENT_WATCH( hubAvatarID, "Hub.AvatarID", "Avatar id the server gave this client", HubWorldComponent, &HubWorldComponentDevInternal::readAvatarID );
    SW_AUTOMATION_COMPONENT_WATCH( hubLevel, "Hub.Level", "Level the server last sent", HubWorldComponent, &HubWorldComponentDevInternal::readLevel );
    SW_AUTOMATION_COMPONENT_WATCH( hubExperience, "Hub.Experience", "Experience the server last sent", HubWorldComponent, &HubWorldComponentDevInternal::readExperience );
    SW_AUTOMATION_COMPONENT_WATCH( hubCoins, "Hub.Coins", "Coins the server last sent", HubWorldComponent, &HubWorldComponentDevInternal::readCoins );
    SW_AUTOMATION_COMPONENT_WATCH( hubHerbs, "Hub.Herbs", "Herbs gathered, as the server last sent", HubWorldComponent, &HubWorldComponentDevInternal::readHerbs );
    SW_AUTOMATION_COMPONENT_WATCH( hubDefeats, "Hub.Defeats", "Creatures felled, as the server last sent", HubWorldComponent, &HubWorldComponentDevInternal::readDefeats );
    SW_AUTOMATION_COMPONENT_WATCH( hubPartySize, "Hub.PartySize", "Party size the server last sent (1 alone)", HubWorldComponent, &HubWorldComponentDevInternal::readPartySize );
    SW_AUTOMATION_COMPONENT_WATCH( hubResultCount, "Hub.ResultCount", "Action results the server sent", HubWorldComponent, &HubWorldComponentDevInternal::readResultCount );
    SW_AUTOMATION_COMPONENT_WATCH( hubLastResult, "Hub.LastResult", "Last HubActionResult the server sent (0 Ok)", HubWorldComponent, &HubWorldComponentDevInternal::readLastResult );
    SW_AUTOMATION_COMPONENT_WATCH( hubVisibleEntities, "Hub.VisibleEntities", "Creatures, herbs and avatars the MMO replication shows this client", HubWorldComponent,
                                   &HubWorldComponentDevInternal::readVisibleEntities );
    SW_AUTOMATION_COMPONENT_WATCH( hubFirstCreatureHealth, "Hub.FirstCreatureHealth", "Replicated health of creature 100000 (no value while felled or unseen)", HubWorldComponent,
                                   &HubWorldComponentDevInternal::readFirstCreatureHealth );
    SW_AUTOMATION_COMPONENT_WATCH( hubAttackReady, "Hub.AttackReady", "1 once the world's server clock is past the attack cooldown the server last sent", HubWorldComponent,
                                   &HubWorldComponentDevInternal::readAttackReady );

    SW_DEV_COMMAND( HubCards, "hub.cards", "hub.cards join | leave | play | draw", "Sit at or leave a card table of the connected hub, play the first legal card (draw without one), or draw", &HubCardTableDevInternal::runCards );
    SW_AUTOMATION_GETTER_WATCH( hubCardsSeat, "Hub.CardsSeat", "Seat at the card table (-1 without one)", HubCardTableComponent, &HubCardTableComponent::getSeat );
    SW_AUTOMATION_GETTER_WATCH( hubCardsStarted, "Hub.CardsStarted", "1 once both seats of the card table are taken", HubCardTableComponent, &HubCardTableComponent::getStartedValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsMyTurn, "Hub.CardsMyTurn", "1 while it is this client's turn at the card table", HubCardTableComponent, &HubCardTableComponent::getMyTurnValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsHand, "Hub.CardsHand", "Cards in this client's hand as dealt by the server", HubCardTableComponent, &HubCardTableComponent::getHandCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsOpponentHand, "Hub.CardsOpponentHand", "Number of cards the other seat holds (the cards themselves never reach this client)", HubCardTableComponent, &HubCardTableComponent::getOpponentCardCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsHUDShown, "Hub.CardsHUDShown", "1 while the HUD shows the hand dealt by the server", HubCardTableComponent, &HubCardTableComponent::getHUDShownValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsActions, "Hub.CardsActions", "Actions the server applied at this client's card table", HubCardTableComponent, &HubCardTableComponent::getActionCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsRejected, "Hub.CardsRejected", "Actions of this client the card table refused", HubCardTableComponent, &HubCardTableComponent::getRejectedCount );
} // namespace sw

namespace sw
{
    namespace
    {
        struct HubPawnReplicationDevInternal
        {
            // 바닥 기준의 자리 — 두 클라이언트 시나리오가 `<Record>` 로 보고에 남기고 `Scripts login-connect-session mmo` 가 견준다(한쪽의 자기 자리와 다른 쪽이 본 자리). 바닥 위가 아니면 값이 없다.
            [[nodiscard]] static bool readFloorX( const HubPawnReplicationComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getFloorLocalPosition()._x );
                return component.getFloorNumber() != 0;
            }
            [[nodiscard]] static bool readFloorZ( const HubPawnReplicationComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getFloorLocalPosition()._z );
                return component.getFloorNumber() != 0;
            }
            [[nodiscard]] static bool readRemoteFloorX( const HubPawnReplicationComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getRemoteFloorLocalPosition()._x );
                return component.getRemoteFloorNumber() != 0;
            }
            [[nodiscard]] static bool readRemoteFloorZ( const HubPawnReplicationComponent& component, float64& outValue )
            {
                outValue = static_cast<float64>( component.getRemoteFloorLocalPosition()._z );
                return component.getRemoteFloorNumber() != 0;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( hubPawnFloorSlip, "Hub.PawnFloorSlip", "Distance the own pawn slid on the moving floor it stands on (metres)", HubPawnReplicationComponent, &HubPawnReplicationComponent::getFloorSlip );
    SW_AUTOMATION_GETTER_WATCH( hubPawnFloor, "Hub.PawnFloor", "Number of the moving floor the own pawn stands on (0 none)", HubPawnReplicationComponent, &HubPawnReplicationComponent::getFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( hubRemotePawnFloor, "Hub.RemotePawnFloor", "Number of the moving floor another player's pawn is shown standing on (0 none)", HubPawnReplicationComponent, &HubPawnReplicationComponent::getRemoteFloorNumber );
    SW_AUTOMATION_COMPONENT_WATCH( hubPawnFloorX, "Hub.PawnFloorX", "Own pawn's X on the moving floor it stands on (floor space; no value off the floor)", HubPawnReplicationComponent, &HubPawnReplicationDevInternal::readFloorX );
    SW_AUTOMATION_COMPONENT_WATCH( hubPawnFloorZ, "Hub.PawnFloorZ", "Own pawn's Z on the moving floor it stands on (floor space; no value off the floor)", HubPawnReplicationComponent, &HubPawnReplicationDevInternal::readFloorZ );
    SW_AUTOMATION_COMPONENT_WATCH( hubRemotePawnFloorX, "Hub.RemotePawnFloorX", "Another player's shown X on the moving floor (floor space; no value off the floor)", HubPawnReplicationComponent, &HubPawnReplicationDevInternal::readRemoteFloorX );
    SW_AUTOMATION_COMPONENT_WATCH( hubRemotePawnFloorZ, "Hub.RemotePawnFloorZ", "Another player's shown Z on the moving floor (floor space; no value off the floor)", HubPawnReplicationComponent, &HubPawnReplicationDevInternal::readRemoteFloorZ );
    SW_AUTOMATION_GETTER_WATCH( hubPawnPredicted, "Hub.PawnPredicted", "1 while this client predicts its own pawn", HubPawnReplicationComponent, &HubPawnReplicationComponent::getPredictedValue );
    SW_AUTOMATION_GETTER_WATCH( hubPawnCorrections, "Hub.PawnCorrections", "Times the server state rewound the predicted pawn", HubPawnReplicationComponent,
                                &HubPawnReplicationComponent::getCorrectionCount );
} // namespace sw
