#include "pch.h"

#include "Games/LanternVale/ValeWorldComponent.h"

#include "Core/File/UserDataPath.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStoreBackend.h"

#include <optional>

namespace sw
{
    namespace
    {
        /** @brief 개발 명령과 감시 값입니다. 골짜기 컴포넌트는 활성 씬에서 찾습니다. */
        struct ValeWorldComponentDevInternal
        {
            /** @brief 골짜기 컴포넌트가 없을 때의 명령 답입니다. */
            static constexpr const utf8* kNoValeReply = "vale: no connected LanternVale client in the active scene";

            /** @brief 활성 씬의 연결된 골짜기 컴포넌트입니다. 없으면 nullptr 이고 부르는 쪽이 `kNoValeReply` 로 답한다. */
            [[nodiscard]] static ValeWorldComponent* findVale()
            {
                const SceneManager* const pScenes  = game::findSubsystem<SceneManager>();
                const Scene* const        pScene   = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const  pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                ValeWorldComponent*       pVale    = nullptr;
                if ( pManager != nullptr )
                {
                    pManager->forEachComponentOfType<ValeWorldComponent>( [&pVale]( ValeWorldComponent* pFound )
                    {
                        if ( pVale == nullptr )
                            pVale = pFound;
                    } );
                }
                return pVale != nullptr && pVale->getClient().isActive() ? pVale : nullptr;
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
                ValeWorldComponent* const pVale = findVale();
                if ( pVale == nullptr )
                {
                    outReply = kNoValeReply;
                    return true;
                }
                const uint32                             sequence = pVale->getClient().callServerFunction( pFunctionName, { ReflectedValue::make( targetID ) } );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "vale: %# %# sent (call %#)", pFunctionName, targetID, sequence );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runAttack( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "attackCreature" ); }
            static bool runGather( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "gatherHerb" ); }
            static bool runInvite( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "inviteToParty" ); }
            static bool runAccept( const vector<string>& listArgument, string& outReply ) { return runIDCommand( listArgument, outReply, "acceptPartyInvite" ); }

            static bool runMove( const vector<string>& listArgument, string& outReply )
            {
                float32 x = 0.0f;
                float32 z = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[0], x ) == false || StringUtil::parseFloat( listArgument[1], z ) == false )
                    return false;
                ValeWorldComponent* const pVale = findVale();
                if ( pVale == nullptr )
                {
                    outReply = kNoValeReply;
                    return true;
                }
                const uint32                             sequence = pVale->getClient().callServerFunction( "moveTo", { ReflectedValue::make( x ), ReflectedValue::make( z ) } );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "vale: moveTo %# %# sent (call %#)", x, z, sequence );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runLeave( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                ValeWorldComponent* const pVale = findVale();
                if ( pVale == nullptr )
                {
                    outReply = kNoValeReply;
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "vale: leaveParty sent (call %#)", pVale->getClient().callServerFunction( "leaveParty", {} ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static bool runStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const ValeWorldComponent* const pVale = findVale();
                if ( pVale == nullptr )
                {
                    outReply = kNoValeReply;
                    return true;
                }
                const ValeAvatarAgent&                   agent = pVale->getClient().getAgent();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "vale: avatar %# level %# experience %# coins %# herbs %# defeats %# party %# visible %#", agent._avatarID, agent._shownLevel, agent._shownExperience,
                                           agent._shownCoins, agent._shownHerbCount, agent._shownDefeatCount, agent._shownPartySize, pVale->getClient().getView().getEntityCount() );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "LanternVale.Status", "[LanternVale] %#", outReply.c_str() );
                return true;
            }

            /** @brief 클라이언트가 서버의 값을 받았을 때만 값이 있습니다(받기 전에는 "값 없음" — 0 과 구분한다). */
            static const ValeAvatarAgent* findShownAgent( const ValeWorldComponent& vale )
            {
                const ValeAvatarAgent& agent = vale.getClient().getAgent();
                return vale.getClient().isActive() && agent._avatarID != 0 ? &agent : nullptr;
            }

            [[nodiscard]] static bool readAvatarID( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_avatarID ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readLevel( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_shownLevel ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readExperience( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_shownExperience ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readCoins( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_shownCoins ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readHerbs( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_shownHerbCount ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readDefeats( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_shownDefeatCount ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readPartySize( const ValeWorldComponent& vale, float64& outValue )
            {
                const ValeAvatarAgent* pAgent = findShownAgent( vale );
                outValue                      = pAgent != nullptr ? static_cast<float64>( pAgent->_shownPartySize ) : 0.0;
                return pAgent != nullptr;
            }

            [[nodiscard]] static bool readResultCount( const ValeWorldComponent& vale, float64& outValue )
            {
                outValue = static_cast<float64>( vale.getClient().getAgent()._resultCount );
                return vale.getClient().isActive();
            }

            [[nodiscard]] static bool readLastResult( const ValeWorldComponent& vale, float64& outValue )
            {
                outValue = static_cast<float64>( static_cast<int32>( vale.getClient().getAgent()._lastResult ) );
                return vale.getClient().isActive() && vale.getClient().getAgent()._resultCount > 0;
            }

            [[nodiscard]] static bool readVisibleEntities( const ValeWorldComponent& vale, float64& outValue )
            {
                outValue = static_cast<float64>( vale.getClient().getView().getEntityCount() );
                return vale.getClient().isActive();
            }

            /** @brief 첫 짐승(`ValeWorld::kPlacementIDBase`)의 복제된 체력입니다. 보이지 않거나 쓰러졌으면 값이 없습니다. */
            [[nodiscard]] static bool readFirstCreatureHealth( const ValeWorldComponent& vale, float64& outValue )
            {
                const MMOEntity* pEntity = vale.getClient().isActive() ? vale.getClient().getView().findEntity( ValeWorld::kPlacementIDBase ) : nullptr;
                if ( pEntity == nullptr )
                    return false;
                BitReader    reader( pEntity->_listState.data(), static_cast<int32>( pEntity->_listState.size() ) );
                const uint64 typeID = reader.readVarUint();
                const int32  health = reader.readVarInt32();
                if ( reader.hasOverflowed() || typeID != ValeServerLink::kCreatureTypeID )
                    return false;
                outValue = static_cast<float64>( health );
                return true;
            }

            /**
             * @brief 마지막 행동 결과를 받은 뒤 공격 재사용 대기(씬 규칙)가 실제 시간으로 지났으면 1 입니다.
             * @details 서버는 결과를 보내기 전에 대기를 시작하므로, 받은 때부터 잰 대기가 지나면 서버의 대기도 지났다. 시나리오가 다음 공격을 이 값으로 기다린다
             *          (고정 프레임 시간은 서버 시계와 다르다).
             */
            [[nodiscard]] static bool readAttackReady( const ValeWorldComponent& vale, float64& outValue )
            {
                if ( vale.getClient().isActive() == false )
                    return false;
                const int64 lastNanoseconds     = vale.getClient().getLastResultNanoseconds();
                const int64 cooldownNanoseconds = MonotonicClock::secondsToNanoseconds( static_cast<float64>( vale.getRules()._attackCooldownSeconds ) );
                const bool  bReady              = lastNanoseconds == 0 || MonotonicClock::nowNanoseconds() >= lastNanoseconds + cooldownNanoseconds;
                outValue                        = bReady ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ValeWorldComponent::ValeWorldComponent()
        : _rules{}
        , _interestGrid{}
        , _profileFolder{}
        , _server{}
        , _client{}
        , _bRulesValid{ SW_FALSE }
        , _bLinkTried{ SW_FALSE }
    {
        setCanEverTick( true );
        _interestGrid._cellSize      = 32.0f;
        _interestGrid._loadingRadius = 96.0f;
    }

    int32 ValeWorldComponent::getNetRoleValue() const
    {
        if ( _server.isActive() )
            return 1;
        return _client.isActive() ? 2 : 0;
    }

    void ValeWorldComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        ValeWorld rulesCheck;
        _bRulesValid = rulesCheck.initialize( _rules ) ? SW_TRUE : SW_FALSE;
        _bLinkTried  = SW_FALSE;
        tryStartLink();
    }

    void ValeWorldComponent::tryStartLink()
    {
        // 세션은 기동 때(전용 서버) 열리거나 플레이 중에(클라이언트 `net.ticketconnect`) 열린다 — 링크가 걸리는 첫 틱에 한 번 세운다.
        const SceneNetLink* pLink = SceneNetLink::findFor( *this );
        if ( _bLinkTried != SW_FALSE || _bRulesValid == SW_FALSE || pLink == nullptr || pLink->isBound() == false )
            return;
        _bLinkTried              = SW_TRUE;
        const NetLinkValue& link = pLink->get();
        if ( link._role == NetLinkRole::Server )
        {
            if ( startServer( *link._pPeers, *link._pRouter ) == false )
                SW_LOG_ERROR_ID( "LanternVale.ServerNotStarted", "[LanternVale] the vale server did not start - see the errors above" );
            return;
        }
        if ( _client.initialize( *link._pPeers, *link._pRouter ) == false )
            SW_LOG_ERROR_ID( "LanternVale.ClientNotStarted", "[LanternVale] the vale client did not start - see the errors above" );
    }

    bool ValeWorldComponent::startServer( INetPeers& peers, NetMessageRouter& router )
    {
        // 두 프로세스 시나리오는 서버의 사용자 데이터 폴더(SW_USER_DATA_ROOT)를 실행마다 빈 폴더로 옮겨 새 진행으로 시작한다 — 저장소는 배포와 같은 파일 저장소다.
        const optional<LocalStoreLocation> location = LocalStoreLocation::tryUnderUserData( _profileFolder );
        if ( location.has_value() == false )
        {
            SW_LOG_ERROR_ID( "LanternVale.ProfileFolderInvalid", "[LanternVale] Profile Folder '%#' must be a relative path under the user data folder", _profileFolder.c_str() );
            return false;
        }
        const LocalStoreConfig            settings{ string( LocalStoreBackend::kFileBackendName ), *location };
        expected<unique_ptr<ILocalStore>> pStore = LocalStoreBackend::createLocalStore( "LanternVale", settings, LocalSealContext{} );
        if ( pStore.has_value() == false )
        {
            SW_LOG_ERROR_ID( "LanternVale.ProfileStoreFailed", "[LanternVale] the profile store did not open: %# (%#)", pStore.error().message(), pStore.error().detail() );
            return false;
        }
        return _server.initialize( peers, router, _rules, _interestGrid, std::move( *pStore ) );
    }

    void ValeWorldComponent::onEndPlay()
    {
        _server.shutdown();
        _client.shutdown();
        Component::onEndPlay();
    }

    void ValeWorldComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        tryStartLink();
        _server.update( deltaTime );
        _client.update();
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( ValeAttack, "vale.attack", "vale.attack <creatureID>", "Ask the vale server to strike a creature (ids start at 100000 in spawn order)", &ValeWorldComponentDevInternal::runAttack );
    SW_DEV_COMMAND( ValeGather, "vale.gather", "vale.gather <herbID>", "Ask the vale server to gather a herb", &ValeWorldComponentDevInternal::runGather );
    SW_DEV_COMMAND( ValeMove, "vale.move", "vale.move <x> <z>", "Ask the vale server to walk this avatar to a point (one step at most Max Move Step)", &ValeWorldComponentDevInternal::runMove );
    SW_DEV_COMMAND( ValeInvite, "vale.invite", "vale.invite <avatarID>", "Invite another avatar to this avatar's party", &ValeWorldComponentDevInternal::runInvite );
    SW_DEV_COMMAND( ValeAccept, "vale.accept", "vale.accept <leaderAvatarID>", "Accept the party invite of an avatar", &ValeWorldComponentDevInternal::runAccept );
    SW_DEV_COMMAND( ValeLeave, "vale.leave", "vale.leave", "Leave the party", &ValeWorldComponentDevInternal::runLeave );
    SW_DEV_COMMAND( ValeStatus, "vale.status", "vale.status", "Show the progress the vale server last sent", &ValeWorldComponentDevInternal::runStatus );

    SW_AUTOMATION_GETTER_WATCH( valeRulesValid, "LanternVale.RulesValid", "1 when the scene's vale rules build a valid vale", ValeWorldComponent, &ValeWorldComponent::getRulesValid );
    SW_AUTOMATION_GETTER_WATCH( valeNetRole, "LanternVale.NetRole", "0 alone, 1 vale server, 2 vale client", ValeWorldComponent, &ValeWorldComponent::getNetRoleValue );
    SW_AUTOMATION_COMPONENT_WATCH( valeAvatarID, "LanternVale.AvatarID", "Avatar id the server gave this client", ValeWorldComponent, &ValeWorldComponentDevInternal::readAvatarID );
    SW_AUTOMATION_COMPONENT_WATCH( valeLevel, "LanternVale.Level", "Level the server last sent", ValeWorldComponent, &ValeWorldComponentDevInternal::readLevel );
    SW_AUTOMATION_COMPONENT_WATCH( valeExperience, "LanternVale.Experience", "Experience the server last sent", ValeWorldComponent, &ValeWorldComponentDevInternal::readExperience );
    SW_AUTOMATION_COMPONENT_WATCH( valeCoins, "LanternVale.Coins", "Coins the server last sent", ValeWorldComponent, &ValeWorldComponentDevInternal::readCoins );
    SW_AUTOMATION_COMPONENT_WATCH( valeHerbs, "LanternVale.Herbs", "Herbs gathered, as the server last sent", ValeWorldComponent, &ValeWorldComponentDevInternal::readHerbs );
    SW_AUTOMATION_COMPONENT_WATCH( valeDefeats, "LanternVale.Defeats", "Creatures felled, as the server last sent", ValeWorldComponent, &ValeWorldComponentDevInternal::readDefeats );
    SW_AUTOMATION_COMPONENT_WATCH( valePartySize, "LanternVale.PartySize", "Party size the server last sent (1 alone)", ValeWorldComponent, &ValeWorldComponentDevInternal::readPartySize );
    SW_AUTOMATION_COMPONENT_WATCH( valeResultCount, "LanternVale.ResultCount", "Action results the server sent", ValeWorldComponent, &ValeWorldComponentDevInternal::readResultCount );
    SW_AUTOMATION_COMPONENT_WATCH( valeLastResult, "LanternVale.LastResult", "Last ValeActionResult the server sent (0 Ok)", ValeWorldComponent, &ValeWorldComponentDevInternal::readLastResult );
    SW_AUTOMATION_COMPONENT_WATCH( valeVisibleEntities, "LanternVale.VisibleEntities", "Creatures, herbs and avatars the MMO replication shows this client", ValeWorldComponent,
                                   &ValeWorldComponentDevInternal::readVisibleEntities );
    SW_AUTOMATION_COMPONENT_WATCH( valeFirstCreatureHealth, "LanternVale.FirstCreatureHealth", "Replicated health of creature 100000 (no value while felled or unseen)", ValeWorldComponent,
                                   &ValeWorldComponentDevInternal::readFirstCreatureHealth );
    SW_AUTOMATION_COMPONENT_WATCH( valeAttackReady, "LanternVale.AttackReady", "1 once the attack cooldown has passed in real time since the last action result", ValeWorldComponent,
                                   &ValeWorldComponentDevInternal::readAttackReady );
} // namespace sw
