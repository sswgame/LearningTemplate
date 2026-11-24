#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/Component/ConquestGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct ConquestGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "CQGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult     = "win";
            static constexpr const utf8* kLoseResult    = "lose";
            static constexpr int32       kStartBuilding = 0;    ///< 시작 건물의 번호 — 판을 열며 처음 짓는다
            static constexpr float32     kAutoPlayAxis  = 1.0f; ///< 자동 플레이의 지휘관 이동 축(오른쪽 — 적 쪽)

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventOrder         = "ConquestOrder";
            static constexpr const utf8* kEventTrained       = "ConquestTrained";
            static constexpr const utf8* kEventRefused       = "ConquestRefused";
            static constexpr const utf8* kEventUnitDown      = "ConquestUnitDown";
            static constexpr const utf8* kEventBroken        = "ConquestBroken";
            static constexpr const utf8* kEventSiteTaken     = "ConquestSiteTaken";
            static constexpr const utf8* kEventSiteLost      = "ConquestSiteLost";
            static constexpr const utf8* kEventWave          = "ConquestWave";
            static constexpr const utf8* kEventCommanderDown = "ConquestCommanderDown";
            static constexpr const utf8* kEventCommanderBack = "ConquestCommanderBack";
            static constexpr const utf8* kEventWon           = "ConquestWon";
            static constexpr const utf8* kEventLost          = "ConquestLost";

            /** @brief 판의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다(거점은 새 주인이 정한다). */
            static const utf8* findSound( ConquestEvent::Kind kind )
            {
                switch ( kind )
                {
                    case ConquestEvent::Kind::UnitTrained:
                    {
                        return kEventTrained;
                    }
                    case ConquestEvent::Kind::UnitDied:
                    {
                        return kEventUnitDown;
                    }
                    case ConquestEvent::Kind::GateBroken:
                    case ConquestEvent::Kind::WallBroken:
                    {
                        return kEventBroken;
                    }
                    case ConquestEvent::Kind::WaveSpawned:
                    {
                        return kEventWave;
                    }
                    case ConquestEvent::Kind::CommanderDied:
                    {
                        return kEventCommanderDown;
                    }
                    case ConquestEvent::Kind::CommanderRespawned:
                    {
                        return kEventCommanderBack;
                    }
                    case ConquestEvent::Kind::Victory:
                    {
                        return kEventWon;
                    }
                    case ConquestEvent::Kind::Defeat:
                    {
                        return kEventLost;
                    }
                    default:
                    {
                        return nullptr;
                    }
                }
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( conquestVictory, "Conquest.Victory", "1 when the enemy holds no site", ConquestGameModeComponent, &ConquestGameModeComponent::getVictory );
    SW_AUTOMATION_GETTER_WATCH( conquestDefeat, "Conquest.Defeat", "1 when the player holds no site", ConquestGameModeComponent, &ConquestGameModeComponent::getDefeat );
    SW_AUTOMATION_GETTER_WATCH( conquestTerritory, "Conquest.Territory", "Sites the player owns", ConquestGameModeComponent, &ConquestGameModeComponent::getTerritory );
    SW_AUTOMATION_GETTER_WATCH( conquestSquad, "Conquest.Squad", "Soldiers in the commander's squad", ConquestGameModeComponent, &ConquestGameModeComponent::getSquadSize );
    SW_AUTOMATION_GETTER_WATCH( conquestCommanderX, "Conquest.CommanderX", "Commander X (m)", ConquestGameModeComponent, &ConquestGameModeComponent::getCommanderX );

    ConquestGameModeComponent::ConquestGameModeComponent()
        : _startBuilding{}
        , _startSite{}
        , _trainUnit{}
        , _resultHoldSeconds{ 3.0f }
        , _winReward{ 0 }
        , _pCatalog{}
        , _world{}
        , _eventLease{}
        , _listEvent{}
        , _resultHold{}
        , _bResultShown{ SW_FALSE }
        , _bFinishRequested{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ConquestGameModeComponent::~ConquestGameModeComponent() = default;

    AbsolutePosition ConquestGameModeComponent::makePosition( float32 x )
    {
        return AbsolutePosition{
            float3{ x, 0.0f, 0.0f }
        };
    }

    bool ConquestGameModeComponent::validateLayout() const
    {
        const ConquestBuildingDef* const pBuilding  = _pCatalog->findBuilding( _startBuilding.key() );
        const bool                       bTrainable = pBuilding != nullptr && VectorUtil::contains( pBuilding->_listTrainable, _trainUnit.key() );
        if ( bTrainable && _pCatalog->findSite( _startSite.key() ) != nullptr && _pCatalog->findUnit( _trainUnit.key() ) != nullptr )
            return true;
        SW_LOG_ERROR_ID( "Conquest.LayoutUnusable",
                         "Conquest game mode: building '%#', site '%#' and unit '%#' must be in the catalog and the building must train the unit - the match does not open", _startBuilding.c_str(),
                         _startSite.c_str(), _trainUnit.c_str() );
        return false;
    }

    bool ConquestGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || validateLayout() == false )
            return false;
        if ( _world.initialize( *_pCatalog ) == false )
            return false; // 까닭은 판이 남겼다
        const ConquestResult placed = _world.placeBuilding( _startBuilding.key(), _startSite.key() );
        if ( placed != ConquestResult::Ok )
        {
            SW_LOG_ERROR_ID( "Conquest.StartBuildingRefused", "Conquest game mode: building '%#' cannot stand at '%#' (result %#) - the match does not open", _startBuilding.c_str(), _startSite.c_str(),
                             static_cast<int32>( placed ) );
            return false;
        }
        if ( _eventLease.isHeld() == false && _world.claimEvents( _eventLease ) == false )
            return false; // 이 디렉터의 표뿐이라 일어날 수 없다 — 둘째 소비자가 생기면 게이트가 어서션을 남긴다
        _listEvent.clear();
        _resultHold.clear();
        return true;
    }

    void ConquestGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Conquest.Started", "[Conquest] %# of %# sites held - the commander waits for commands (move, order, train)", getTerritory(), _world.getSites().size() );
    }

    bool ConquestGameModeComponent::isAwaitingCommand() const
    {
        return isStarted() && _world.isVictory() == false && _world.isDefeat() == false && isAutoPlayOn() == false;
    }

    bool ConquestGameModeComponent::submitCommanderMove( float32 axis )
    {
        if ( isAwaitingCommand() == false )
            return false;
        _world.setCommanderMove( axis );
        return true;
    }

    bool ConquestGameModeComponent::submitOrder( ConquestOrder order )
    {
        if ( isAwaitingCommand() == false )
            return false;
        _world.issueOrder( order );
        getSoundQueue().queueEvent( ConquestGameModeComponentInternal::kEventOrder );
        SW_LOG_INFO_ID( "Conquest.Order", "[Conquest] order %# to a squad of %#", static_cast<int32>( order ), _world.getSquadSize() );
        return true;
    }

    ConquestResult ConquestGameModeComponent::submitTrain()
    {
        if ( isAwaitingCommand() == false )
            return ConquestResult::InvalidBuilding;
        const ConquestResult result = _world.trainUnit( ConquestGameModeComponentInternal::kStartBuilding, _trainUnit.key() );
        if ( result != ConquestResult::Ok )
        {
            getSoundQueue().queueEvent( ConquestGameModeComponentInternal::kEventRefused );
            SW_LOG_INFO_ID( "Conquest.TrainingRefused", "[Conquest] '%#' is not trained now (result %#)", _trainUnit.c_str(), static_cast<int32>( result ) );
        }
        return result;
    }

    bool ConquestGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        const GameObjectManager* const pManager = getObjectManager();
        outFocusObject                          = GameObjectHandle{};
        if ( isStarted() == false || pManager == nullptr )
            return false;
        outFocusPosition = pManager->getWorldOrigin().toLocal( makePosition( getCommanderX() ) );
        return true;
    }

    void ConquestGameModeComponent::stepAutoPlay()
    {
        (void)_world.trainUnit( ConquestGameModeComponentInternal::kStartBuilding, _trainUnit.key() ); // 자원 · 인구가 모자라면 다음 걸음에 다시 해 본다
        _world.issueOrder( ConquestOrder::Charge );
        _world.setCommanderMove( ConquestGameModeComponentInternal::kAutoPlayAxis );
    }

    void ConquestGameModeComponent::reportEvents()
    {
        _listEvent.clear();
        _world.pollEvents( _eventLease, _listEvent );
        for ( const ConquestEvent& event : _listEvent )
        {
            const utf8* pSound = ConquestGameModeComponentInternal::findSound( event._kind );
            if ( event._kind == ConquestEvent::Kind::SiteCaptured )
                pSound = event._team == ConquestTeam::Player ? ConquestGameModeComponentInternal::kEventSiteTaken : ConquestGameModeComponentInternal::kEventSiteLost;
            if ( pSound != nullptr )
                getSoundQueue().queueEvent( pSound );
            if ( event._kind == ConquestEvent::Kind::SiteCaptured )
                SW_LOG_INFO_ID( "Conquest.SiteCaptured", "[Conquest] site '%#' taken by team %#", event._id.c_str(), static_cast<int32>( event._team ) );
            else if ( event._kind == ConquestEvent::Kind::GateBroken )
                SW_LOG_INFO_ID( "Conquest.GateBroken", "[Conquest] the gate of '%#' is broken", event._id.c_str() );
            else if ( event._kind == ConquestEvent::Kind::UnitTrained )
                SW_LOG_INFO_ID( "Conquest.UnitTrained", "[Conquest] '%#' trained", event._id.c_str() );
            else if ( event._kind == ConquestEvent::Kind::Victory )
                SW_LOG_INFO_ID( "Conquest.Won", "[Conquest] victory after %# s", _world.getElapsed() );
            else if ( event._kind == ConquestEvent::Kind::Defeat )
                SW_LOG_INFO_ID( "Conquest.Lost", "[Conquest] defeat after %# s", _world.getElapsed() );
        }
    }

    void ConquestGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _bFinishRequested == SW_TRUE )
            return;
        const bool bOver = _world.isVictory() || _world.isDefeat();
        if ( bOver == false && isAutoPlayOn() )
            stepAutoPlay();
        _world.update( deltaTime );
        reportEvents();
        if ( _world.isVictory() == false && _world.isDefeat() == false )
            return;
        if ( _bResultShown == SW_FALSE )
        {
            _bResultShown = SW_TRUE;
            _resultHold.start( _resultHoldSeconds );
            return;
        }
        if ( _resultHold.update( deltaTime ) == false )
            return;
        _bFinishRequested = SW_TRUE;
        (void)finishMinigame( hashed_string( _world.isVictory() ? ConquestGameModeComponentInternal::kWinResult : ConquestGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
    }

    void ConquestGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _world.isVictory() )
            addMoneyResult( outListValue, _winReward );
    }

    void ConquestGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`ConquestViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void ConquestGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( ConquestGameModeComponentInternal::kStateTag, ConquestGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 판은 같은 카탈로그로 `initialize` 한 뒤에 읽는다(키트 계약). 꺼내기 표는 대입이 옮기지 않아 이 디렉터의 표가 그대로다.
        ConquestWorld restoredWorld;
        Countdown     resultHold = _resultHold;
        if ( io.isReading() && io.check( _pCatalog != nullptr && restoredWorld.initialize( *_pCatalog ) ) == false )
            return;
        io.value( io.isWriting() ? _world : restoredWorld );
        io.value( resultHold );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _world      = std::move( restoredWorld );
        _resultHold = resultHold;
    }
} // namespace sw
