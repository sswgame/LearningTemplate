#include "pch.h"

#include "Games/Playground/Hub/HubWorldComponent.h"

#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

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
    } // namespace
} // namespace sw

namespace sw
{
    HubWorldComponent::HubWorldComponent()
        : _rules{}
        , _interestGrid{}
        , _pServer{}
        , _client{}
        , _linkSessionSerial{ 0 }
        , _bRulesValid{ SW_FALSE }
        , _bLinkTried{ SW_FALSE }
    {
        setCanEverTick( true );
        _interestGrid._cellSize      = 32.0f;
        _interestGrid._loadingRadius = 96.0f;
    }

    int32 HubWorldComponent::getNetRoleValue() const
    {
        if ( _pServer != nullptr )
            return 1;
        return _client.isActive() ? 2 : 0;
    }

    void HubWorldComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        HubWorld rulesCheck;
        _bRulesValid = rulesCheck.initialize( _rules ) ? SW_TRUE : SW_FALSE;
        _bLinkTried  = SW_FALSE;
        tryStartLink();
    }

    void HubWorldComponent::tryStartLink()
    {
        // 세션은 기동 때(전용 서버) 열리거나 플레이 중에(클라이언트 `net.ticketconnect` · 온라인 흐름) 열린다 — 링크가 걸리는 첫 틱에 한 번 세운다.
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        if ( _bLinkTried != SW_FALSE && ( pLink == nullptr || pLink->_sessionSerial != _linkSessionSerial ) )
        {
            stopLink(); // 매치 서버로 옮기며 허브 세션을 떠났거나 서버가 세션을 닫았다(틱 사이에 새 세션이 열린 것까지 번호로 가린다) — 다시 걸리면 새로 세운다
            _bLinkTried = SW_FALSE;
        }
        if ( _bLinkTried != SW_FALSE || _bRulesValid == SW_FALSE || pLink == nullptr )
            return;
        _bLinkTried              = SW_TRUE;
        _linkSessionSerial       = pLink->_sessionSerial;
        const NetLinkValue& link = *pLink;
        if ( link._role == NetLinkRole::Server )
        {
            // 허브는 계정마다 진행을 저장한다 — 티켓을 요구하지 않는 세션(개발 · 리슨 호스트)에는 계정이 없어 허브 서버를 세우지 않는다. 접속한 클라이언트는 아바타 없이 같은 씬에 있다.
            if ( link._bTicketRequired == SW_FALSE )
            {
                SW_LOG_INFO_ID( "Hub.NoTicketSession", "[Hub] this session does not require game connect tickets (server config _bGameConnectTicket) - the hub server does not start" );
                return;
            }
            if ( startServer( *link._pPeers, *link._pRouter ) == false )
                SW_LOG_ERROR_ID( "Hub.ServerNotStarted", "[Hub] the hub server did not start - see the errors above" );
            return;
        }
        if ( _client.initialize( *link._pPeers, *link._pRouter ) == false )
            SW_LOG_ERROR_ID( "Hub.ClientNotStarted", "[Hub] the hub client did not start - see the errors above" );
    }

    bool HubWorldComponent::startServer( INetPeers& peers, NetMessageRouter& router )
    {
        // 서버 링크는 서버 전용 코드다 — 클라이언트 구성에는 공장이 없다. 진행 저장소는 전용 서버의 온라인 조립 저장소다(공장이 찾는다).
        IHubServerFactory* const pFactory = game::findSubsystem<IHubServerFactory>();
        if ( pFactory == nullptr )
        {
            SW_LOG_ERROR_ID( "Hub.ServerCodeMissing", "[Hub] this build has no hub server code (client configuration) - the server role cannot start" );
            return false;
        }
        _pServer = pFactory->createServer( peers, router, _rules, _interestGrid );
        return _pServer != nullptr;
    }

    void HubWorldComponent::stopLink()
    {
        if ( _pServer != nullptr )
            _pServer->shutdown();
        _pServer.reset();
        _client.shutdown();
    }

    void HubWorldComponent::onEndPlay()
    {
        stopLink();
        Component::onEndPlay();
    }

    void HubWorldComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        tryStartLink();
        if ( _pServer != nullptr )
            _pServer->update( context.getDeltaTime().count() );
        _client.update();
    }
} // namespace sw

namespace sw
{
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
} // namespace sw
