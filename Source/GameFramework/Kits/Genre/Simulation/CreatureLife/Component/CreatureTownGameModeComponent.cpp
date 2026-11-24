#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/Component/CreatureTownGameModeComponent.h"

#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    CreatureTownGameModeComponent::CreatureTownGameModeComponent()
        : _creatureDataPath{}
        , _questDataPath{}
        , _friendshipDataPath{}
        , _townSize{ 0 }
        , _fixedWeatherID{}
        , _listStartObject{}
        , _upgradeHabitat{}
        , _upgradePrice{ 0 }
        , _listUpgradeObject{}
        , _pCatalog{}
        , _pQuestCatalog{}
        , _pFriendshipCatalog{}
        , _town{}
        , _townRevision{ 0 }
    {
    }

    CreatureTownGameModeComponent::~CreatureTownGameModeComponent() = default;

    void CreatureTownGameModeComponent::serializeGameState( StateIO& io )
    {
        GameStateComponent*  pState      = io.isReading() ? GameStateComponent::findOnOwner( *this ) : nullptr;
        ReputationComponent* pReputation = io.isReading() ? findOnPlayerState<ReputationComponent>() : nullptr;
        QuestLogComponent*   pQuestLog   = io.isReading() ? findOnPlayerState<QuestLogComponent>() : nullptr;
        if ( io.check( pState != nullptr && pReputation != nullptr && pQuestLog != nullptr ) == false )
            return;
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        CreatureTown restoredTown;
        if ( io.isReading() && io.check( initializeTown( restoredTown, *pState, *pReputation, *pQuestLog ) ) == false )
            return;
        CreatureTown& town = io.isWriting() ? _town : restoredTown;
        io.section( CreatureTown::kStateTag, CreatureTown::kStateVersion, [&town]( StateIO& bodyIO )
        { bodyIO.value( town ); } );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _town = std::move( restoredTown );
        ++_townRevision;
    }

    bool CreatureTownGameModeComponent::initializeTown( CreatureTown& outTown, GameStateComponent& state, ReputationComponent& reputation, QuestLogComponent& questLog ) const
    {
        return _pCatalog != nullptr && outTown.initialize( _pCatalog.get(), reputation.getReputation(), questLog.getQuestLog(), state.getClock(), _townSize, _townSize );
    }

    bool CreatureTownGameModeComponent::startGame()
    {
        // 셋 모두 본다 — 첫 오류에서 멈추면 씬을 고칠 때마다 하나씩 드러난다.
        // 일지 · 평판은 처음 열 때의 카탈로그를 계속 빌린다(`initialize` 는 첫 호출만 듣는다) — 판을 다시 열 때 새 판으로 바꾸면 빌린 쪽이 사라진 카탈로그를 가리킨다.
        const bool bCreaturesLoaded  = acquireRequired( _creatureDataPath, _pCatalog );
        const bool bQuestsLoaded     = _pQuestCatalog != nullptr || acquireRequiredSnapshot( _questDataPath, _pQuestCatalog );
        const bool bFriendshipLoaded = _pFriendshipCatalog != nullptr || acquireRequiredSnapshot( _friendshipDataPath, _pFriendshipCatalog );
        const bool bCatalogsLoaded   = bCreaturesLoaded && bQuestsLoaded && bFriendshipLoaded;
        if ( bCatalogsLoaded == false )
            return false;
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr || pState->isInitialized() == false )
        {
            SW_LOG_ERROR_ID( "CreatureTown.StateNotOpen", "[CreatureTown] the town cannot start - no game mode in front of it on this object opened the shared state" );
            return false;
        }
        // 지갑은 열기가 없다 — 마을 업그레이드이 쓰므로 판을 열 때 있는지 본다.
        const GameObject* const pPlayerState = requirePlayerState();
        if ( pPlayerState == nullptr || PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState ) == nullptr )
            return false; // 까닭은 찾기가 남겼다
        QuestLogComponent*   pQuestLog   = PlayerComponentUtil::openOn<QuestLogComponent>( *pPlayerState, *_pQuestCatalog );
        ReputationComponent* pReputation = PlayerComponentUtil::openOn<ReputationComponent>( *pPlayerState, *_pFriendshipCatalog );
        if ( pQuestLog == nullptr || pReputation == nullptr || initializeTown( _town, *pState, *pReputation, *pQuestLog ) == false )
            return false;
        if ( pState->isFreshGame() && placeObjects( _listStartObject ) == false )
            return false;
        ++_townRevision;
        return true;
    }

    bool CreatureTownGameModeComponent::submitTalk( const hashed_string& speciesID )
    {
        if ( isStarted() == false )
            return false;
        const CreatureInteractResult result = _town.talkTo( speciesID );
        SW_LOG_INFO_ID( "CreatureTown.Talk", "[CreatureTown] talk to '%#': %# (friendship %#)", speciesID.c_str(), getEnumName( result ), _town.getFriendship( speciesID ) );
        return result == CreatureInteractResult::Ok;
    }

    void CreatureTownGameModeComponent::updateGame( float32 deltaTime )
    {
        (void)deltaTime;
        GameStateComponent* pState    = GameStateComponent::findOnOwner( *this );
        QuestLogComponent*  pQuestLog = findOnPlayerState<QuestLogComponent>();
        WalletComponent*    pWallet   = findOnPlayerState<WalletComponent>();
        if ( pState == nullptr || pQuestLog == nullptr || pWallet == nullptr )
            return;
        const WorldClock&    clock    = pState->getClock();
        const WeatherSystem* pWeather = pState->findWeather();
        const hashed_string  weather  = pWeather != nullptr ? pWeather->getCurrent() : _fixedWeatherID;
        for ( const WorldClockEvent& clockEvent : pState->getClockEvents() )
        {
            if ( clockEvent._kind == WorldClockEvent::Kind::HourChanged )
            {
                if ( 0 < _town.attractVisitorsAt( clock.getDay(), clockEvent._value, clock.getDayPhase(), weather ) )
                {
                    SW_LOG_INFO_ID( "CreatureTown.CreatureMovedIn", "[CreatureTown] a creature moved in on day %#", clock.getDay() );
                    ++_townRevision;
                }
            }
            else if ( clockEvent._kind == WorldClockEvent::Kind::DayChanged )
            {
                _town.advanceDay();
            }
        }
        startOfferedRequests( pQuestLog->getQuestLog() );
        // 앞 디렉터가 이번 틱에 번 돈도 여기서 보인다 — 같은 오브젝트에서 먼저 돈다.
        buyUpgrade( pWallet->getWallet(), clock.getDay() );
    }

    bool CreatureTownGameModeComponent::placeObjects( const vector<CreatureTownPlacement>& listPlacement )
    {
        bool bPlaced = true;
        for ( const CreatureTownPlacement& placement : listPlacement )
        {
            if ( _town.setObject( placement._x, placement._y, placement._object.key() ) )
                continue;
            SW_LOG_ERROR_ID( "CreatureTown.PlacementRefused", "[CreatureTown] object '%#' at (%#, %#) was refused - check the scene layout and the town size", placement._object.c_str(), placement._x, placement._y );
            bPlaced = false;
        }
        return bPlaced;
    }

    void CreatureTownGameModeComponent::startOfferedRequests( const QuestLog& questLog )
    {
        // 부탁을 받아도 생물 목록은 그대로다(호감도 · 일지만 바뀐다) — 도는 동안 안전하다.
        for ( const TownCreature& creature : _town.getCreatures() )
        {
            const CreatureSpeciesDef* const pSpecies = _pCatalog->findSpecies( creature._speciesID );
            if ( pSpecies == nullptr )
                continue;
            for ( const hashed_string& request : pSpecies->_listRequest )
            {
                if ( questLog.getStatus( request ) == QuestStatus::NotStarted && _town.startRequest( creature._speciesID, request ) == CreatureRequestResult::Ok )
                    SW_LOG_INFO_ID( "CreatureTown.RequestStarted", "[CreatureTown] '%#' asks for '%#'", creature._speciesID.c_str(), request.c_str() );
            }
        }
    }

    void CreatureTownGameModeComponent::buyUpgrade( Wallet& wallet, int32 day )
    {
        const bool bCanBuy = _upgradeHabitat.empty() == false && _town.countHabitats( _upgradeHabitat.key() ) == 0 && wallet.canAfford( Wallet::getDefaultCurrency(), _upgradePrice );
        if ( bCanBuy == false || wallet.trySpend( Wallet::getDefaultCurrency(), _upgradePrice ) == false )
            return;
        (void)placeObjects( _listUpgradeObject ); // 놓지 못한 줄은 오류를 남겼다
        ++_townRevision;
        SW_LOG_INFO_ID( "CreatureTown.UpgradeBought", "[CreatureTown] '%#' built on day %#", _upgradeHabitat.c_str(), day );
    }

    void CreatureTownGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 클라이언트 모듈(`CreatureTownViewComponent`)이 세운다.
        (void)manager;
        (void)bRespawnViews;
    }
} // namespace sw
