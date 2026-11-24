#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Component/SurvivalHorrorGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct SurvivalHorrorGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "SHGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( survivalHorrorResult, "SurvivalHorror.Result", "Result (0 ongoing, 1 win, 2 lose)", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorPlayerX, "SurvivalHorror.PlayerX", "Absolute X of the player (m)", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorItems, "SurvivalHorror.Items", "Stacks in the bag", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getItemCount );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorSanity, "SurvivalHorror.Sanity", "Sanity of the player", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getSanity );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorHealth, "SurvivalHorror.Health", "Health of the player", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::getHealth );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorFlashlight, "SurvivalHorror.Flashlight", "1 while the flashlight is on", SurvivalHorrorGameModeComponent,
                                &SurvivalHorrorGameModeComponent::isFlashlightOn );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorDark, "SurvivalHorror.Dark", "1 while the player is in a dark room", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::isInDarkness );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorFighting, "SurvivalHorror.Fighting", "1 during a fight", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::isFighting );
    SW_AUTOMATION_GETTER_WATCH( survivalHorrorMonsters, "SurvivalHorror.Monsters", "Monsters left", SurvivalHorrorGameModeComponent, &SurvivalHorrorGameModeComponent::countMonsters );

    SurvivalHorrorGameModeComponent::SurvivalHorrorGameModeComponent()
        : _areaGraph{}
        , _stage{}
        , _boxSlotCount{ 8 }
        , _seed{ 3 }
        , _moveSpeed{ 4.0f }
        , _useRadius{ 1.5f }
        , _investigator{}
        , _ammoItem{}
        , _ammoStrength{ 0 }
        , _damagePerPoint{ 10.0f }
        , _turnSeconds{ 0.5f }
        , _resultHoldSeconds{ 3.0f }
        , _winReward{ 0 }
        , _pCatalog{}
        , _pGraphAsset{}
        , _graph{}
        , _flags{}
        , _bag{}
        , _itemBox{}
        , _session{}
        , _encounter{}
        , _command{}
        , _listEvent{}
        , _listUsedSpot{}
        , _turn{}
        , _resultHold{}
        , _playerPosition{ 0.0f, 0.0f }
        , _encounterSpot{ constant::invalid_index::kInt32 }
        , _bArmed{ SW_FALSE }
        , _result{ SurvivalHorrorResult::Ongoing }
    {
    }

    SurvivalHorrorGameModeComponent::~SurvivalHorrorGameModeComponent() = default;

    bool SurvivalHorrorGameModeComponent::openSession()
    {
        _graph = *_pGraphAsset;
        _graph.resetState();
        _flags.clear();
        return _itemBox.initialize( nullptr, _boxSlotCount ) && _session.initialize( _pCatalog.get(), &_graph, _stage._startArea, &_flags, _bag, _itemBox );
    }

    bool SurvivalHorrorGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireRequired( _areaGraph, _pGraphAsset ) == false )
            return false;
        if ( _stage.validate( *_pCatalog, *_pGraphAsset ) == false || openSession() == false )
            return false;
        _command = SurvivalHorrorCommand{};
        _listUsedSpot.assign( _stage._listSpot.size(), SW_FALSE );
        _playerPosition = _graph.findArea( _stage._startArea )->getCenter(); // 있는 방인지는 `validate` 가 보았다
        _turn.clear();
        _resultHold.clear();
        witnessMonsters();
        return true;
    }

    void SurvivalHorrorGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "SurvivalHorror.Started", "[SurvivalHorror] started in '%#' - find the way to '%#'", _stage._startArea.c_str(), _stage._goalArea.c_str() );
    }

    void SurvivalHorrorGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            return;
        SW_LOG_WARNING_ID( "SurvivalHorror.StateRefused", "[SurvivalHorror] the saved game does not fit this build - a new game opens" );
        (void)openGame(); // 새 판도 열지 못하면 startGame 이 이유를 남겼다
    }

    bool SurvivalHorrorGameModeComponent::submitPlayerInput( const SurvivalHorrorCommand& command )
    {
        if ( isAcceptingInput() == false )
            return false;
        _command._move = command._move;
        if ( command._bUseTriggered == SW_TRUE )
            _command._bUseTriggered = SW_TRUE;
        if ( command._bLightTriggered == SW_TRUE )
            _command._bLightTriggered = SW_TRUE;
        if ( command._bHealTriggered == SW_TRUE )
            _command._bHealTriggered = SW_TRUE;
        return true;
    }

    bool SurvivalHorrorGameModeComponent::isInDarkness() const
    {
        const AreaDef* const pArea = _graph.findArea( _session.getCurrentArea() );
        return pArea != nullptr && _stage._darkRegion.empty() == false && pArea->_region == _stage._darkRegion;
    }

    float32 SurvivalHorrorGameModeComponent::getHealthRatio() const
    {
        return _pCatalog != nullptr ? _session.getHealth() / _pCatalog->getRules()._maxHealth : 0.0f;
    }

    int32 SurvivalHorrorGameModeComponent::countMonsters() const
    {
        int32 count = 0;
        for ( size_t spot = 0; spot < _stage._listSpot.size(); ++spot )
        {
            count += _stage._listSpot[spot]._kind == SurvivalHorrorSpotKind::Monster && isSpotUsed( spot ) == false ? 1 : 0;
        }
        return count;
    }

    void SurvivalHorrorGameModeComponent::witnessMonsters()
    {
        for ( size_t spot = 0; spot < _stage._listSpot.size(); ++spot )
        {
            const SurvivalHorrorSpot& place = _stage._listSpot[spot];
            if ( place._kind == SurvivalHorrorSpotKind::Monster && place._area == _session.getCurrentArea() && isSpotUsed( spot ) == false )
                (void)_session.witnessMonster( place._id ); // 깎인 양은 이벤트로 온다
        }
    }

    void SurvivalHorrorGameModeComponent::movePlayer( const float2& delta )
    {
        const AreaWalk walk = _graph.walk( _session.getCurrentArea(), _playerPosition, delta, _flags );
        _playerPosition     = walk._position;
        if ( walk._pBlocked != nullptr && _pCatalog->findKeyLock( walk._pBlocked->_kind ) != nullptr )
            (void)_session.useKey( walk._pBlocked->_kind ); // 연결의 종류가 자물쇠 id 다 — 열쇠가 없으면 그대로 막힌다
        if ( walk._pEntered != nullptr && _session.tryMoveTo( walk._pEntered->_id ) )
            witnessMonsters();
    }

    void SurvivalHorrorGameModeComponent::use()
    {
        size_t  nearest = _stage._listSpot.size();
        float32 best    = _useRadius;
        for ( size_t spot = 0; spot < _stage._listSpot.size(); ++spot )
        {
            const SurvivalHorrorSpot& place    = _stage._listSpot[spot];
            const float32             distance = ( place._position - _playerPosition ).getLength();
            if ( place._area != _session.getCurrentArea() || isSpotUsed( spot ) || distance > best )
                continue;
            best    = distance;
            nearest = spot;
        }
        if ( nearest < _stage._listSpot.size() )
            useSpot( nearest );
    }

    void SurvivalHorrorGameModeComponent::useSpot( size_t spotIndex )
    {
        const SurvivalHorrorSpot& spot = _stage._listSpot[spotIndex];
        switch ( spot._kind )
        {
            case SurvivalHorrorSpotKind::Pickup:
            {
                if ( _session.getInventory().addItem( spot._id, spot._count ) > 0 )
                    _listUsedSpot[spotIndex] = SW_TRUE;
                break;
            }
            case SurvivalHorrorSpotKind::Document:
            {
                (void)_session.readDocument( spot._id ); // 이미 읽은 문서는 아무 일도 없다
                break;
            }
            case SurvivalHorrorSpotKind::SequenceStep:
            {
                (void)_session.pressSequenceStep( spot._id, spot._answer );
                break;
            }
            case SurvivalHorrorSpotKind::DialLock:
            {
                const SurvivalHorrorDialLockDef* const pLock = _pCatalog->findDialLock( spot._id );
                if ( pLock != nullptr && ( spot._clue.empty() || _session.hasClue( spot._clue ) ) )
                    (void)_session.enterDialCode( spot._id, pLock->_listDigit ); // 단서가 번호를 알려 준다
                break;
            }
            case SurvivalHorrorSpotKind::Deduction:
            {
                submitDeduction( spot );
                break;
            }
            case SurvivalHorrorSpotKind::SavePoint:
            {
                (void)_session.trySave(); // 되면 Saved 이벤트가 온다
                break;
            }
            case SurvivalHorrorSpotKind::ItemBox:
            {
                useItemBox();
                break;
            }
            case SurvivalHorrorSpotKind::Monster:
            {
                startEncounter( spotIndex );
                break;
            }
        }
    }

    void SurvivalHorrorGameModeComponent::useItemBox()
    {
        for ( int32 slot = 0; slot < _itemBox.getSlotCount(); ++slot )
        {
            const InventorySlot& stored = _itemBox.getSlot( slot );
            if ( stored.isEmpty() )
                continue;
            const hashed_string itemID = stored._itemID;
            (void)_session.takeFromBox( itemID, stored._count ); // 가방이 차면 남은 것은 상자에 있다
            return;
        }
        for ( const GridItem& item : _bag.getItems() )
        {
            const SurvivalHorrorItemDef* const pItem = _pCatalog->findItem( item._itemID );
            if ( pItem == nullptr || pItem->_kind == SurvivalHorrorItemKind::Key )
                continue;
            const int32 instanceID = item._instanceID;
            const int32 count      = item._count;
            (void)_session.storeInBox( instanceID, count ); // 상자가 차면 거절한다
            return;
        }
    }

    void SurvivalHorrorGameModeComponent::submitDeduction( const SurvivalHorrorSpot& spot )
    {
        const SurvivalHorrorDeductionDef* const pDeduction = _pCatalog->findDeduction( spot._id );
        if ( pDeduction == nullptr )
            return;
        for ( const SurvivalHorrorClueLink& link : pDeduction->_listRequiredLink )
        {
            if ( _session.isLinked( link._first, link._second ) == false )
                (void)_session.linkClues( link._first, link._second ); // 없는 단서면 거절한다
        }
        (void)_session.submitDeduction( spot._id, spot._answer );
    }

    void SurvivalHorrorGameModeComponent::heal()
    {
        vector<int32> listInstance;
        for ( const GridItem& item : _bag.getItems() )
        {
            listInstance.push_back( item._instanceID );
        }
        for ( const int32 instanceID : listInstance )
        {
            if ( _session.tryUseItem( instanceID ) )
                return;
        }
        for ( const int32 firstID : listInstance )
        {
            for ( const int32 secondID : listInstance )
            {
                if ( _session.combineItems( firstID, secondID ) )
                    return;
            }
        }
    }

    vector<SurvivalHorrorInvestigator> SurvivalHorrorGameModeComponent::makeInvestigators() const
    {
        SurvivalHorrorInvestigator investigator = _investigator;
        if ( _bArmed == SW_TRUE )
            investigator._strength += _ammoStrength;
        return vector<SurvivalHorrorInvestigator>{ investigator };
    }

    void SurvivalHorrorGameModeComponent::startEncounter( size_t spotIndex )
    {
        const SurvivalHorrorMonsterDef* const pMonster = _pCatalog->findMonster( _stage._listSpot[spotIndex]._id );
        if ( pMonster == nullptr )
            return;
        _bArmed = _ammoItem.empty() == false && _session.tryConsumeAmmo( _ammoItem.key(), 1 ) ? SW_TRUE : SW_FALSE;
        if ( _encounter.initialize( *pMonster, makeInvestigators(), _seed ) == false )
            return; // 조사자 값이 범위 밖이면 전투가 이유를 남겼다
        _encounterSpot = static_cast<int32>( spotIndex );
        _turn.start( _turnSeconds );
        SW_LOG_INFO_ID( "SurvivalHorror.FightStarted", "[SurvivalHorror] fight with '%#' (%#)", pMonster->_id.c_str(), _bArmed == SW_TRUE ? "armed" : "unarmed" );
    }

    void SurvivalHorrorGameModeComponent::updateEncounter( float32 deltaTime )
    {
        if ( _turn.update( deltaTime ) == false )
            return;
        _turn.restart( _turnSeconds );
        const SurvivalHorrorTurnResult turn = _encounter.playNextTurn();
        _session.applyDamage( static_cast<float32>( turn._healthLoss ) * _damagePerPoint );
        if ( turn._sanityLoss > 0 )
            _session.loseSanity( static_cast<float32>( turn._sanityLoss ) * _damagePerPoint, _stage._listSpot[static_cast<size_t>( _encounterSpot )]._id );
        if ( _encounter.getState() == SurvivalHorrorEncounterState::Ongoing )
            return;
        const bool bVictory = _encounter.getState() == SurvivalHorrorEncounterState::Victory;
        if ( bVictory )
            _listUsedSpot[static_cast<size_t>( _encounterSpot )] = SW_TRUE;
        else
            _session.applyDamage( _session.getHealth() ); // 쓰러졌다
        SW_LOG_INFO_ID( "SurvivalHorror.FightEnded", "[SurvivalHorror] fight %# in %# turns", bVictory ? "won" : "lost", _encounter.getTurnCount() );
        _encounterSpot = constant::invalid_index::kInt32;
        _bArmed        = SW_FALSE;
    }

    void SurvivalHorrorGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _result != SurvivalHorrorResult::Ongoing )
        {
            using Internal = SurvivalHorrorGameModeComponentInternal;
            if ( _resultHold.update( deltaTime ) )
                (void)finishMinigame( hashed_string( _result == SurvivalHorrorResult::Win ? Internal::kWinResult : Internal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
            return;
        }
        const SurvivalHorrorCommand command = _command;
        _command                            = SurvivalHorrorCommand{}; // 쓴 명령이다(이동은 클라이언트가 다음 프레임에 다시 넣는다)
        if ( isFighting() )
        {
            updateEncounter( deltaTime );
        }
        else
        {
            const float32 moveLength = command._move.getLength();
            if ( moveLength > MathUtil::kEpsilon )
                movePlayer( command._move * ( _moveSpeed * deltaTime / MathUtil::max( 1.0f, moveLength ) ) );
            if ( command._bUseTriggered == SW_TRUE )
                use();
            if ( command._bHealTriggered == SW_TRUE )
                heal();
        }
        if ( command._bLightTriggered == SW_TRUE )
            (void)_session.trySetFlashlight( _session.isFlashlightOn() == false ); // 배터리가 없으면 켜지지 않는다
        _session.update( deltaTime, isInDarkness() );
        settle();
    }

    void SurvivalHorrorGameModeComponent::settle()
    {
        using Kind = SurvivalHorrorEvent::Kind;
        _listEvent.clear();
        _session.pollEvents( _listEvent );
        for ( const SurvivalHorrorEvent& event : _listEvent )
        {
            if ( event._kind == Kind::DoorUnlocked )
                SW_LOG_INFO_ID( "SurvivalHorror.DoorUnlocked", "[SurvivalHorror] unlocked '%#'", event._id.c_str() );
            else if ( event._kind == Kind::PuzzleSolved )
                SW_LOG_INFO_ID( "SurvivalHorror.PuzzleSolved", "[SurvivalHorror] solved '%#'", event._id.c_str() );
            else if ( event._kind == Kind::DocumentRead )
                SW_LOG_INFO_ID( "SurvivalHorror.DocumentRead", "[SurvivalHorror] read '%#'", event._id.c_str() );
            else if ( event._kind == Kind::Saved )
                SW_LOG_INFO_ID( "SurvivalHorror.Saved", "[SurvivalHorror] save point used %# times", event._value );
        }
        if ( _session.isDead() )
            _result = SurvivalHorrorResult::Lose;
        else if ( _session.getCurrentArea() == _stage._goalArea )
            _result = SurvivalHorrorResult::Win;
        if ( _result == SurvivalHorrorResult::Ongoing )
            return;
        _resultHold.start( _resultHoldSeconds );
        SW_LOG_INFO_ID( "SurvivalHorror.Ended", "[SurvivalHorror] %# - health %#, sanity %#", _result == SurvivalHorrorResult::Win ? "got out" : "died", getHealth(), getSanity() );
    }

    void SurvivalHorrorGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _result == SurvivalHorrorResult::Win )
            addMoneyResult( outListValue, _winReward );
    }

    bool SurvivalHorrorGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPlayerPosition()._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( getPlayerPosition() );
        return isStarted();
    }

    void SurvivalHorrorGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`SurvivalHorrorAvatarComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void SurvivalHorrorGameModeComponent::serializeGameState( StateIO& io )
    {
        using Internal = SurvivalHorrorGameModeComponentInternal;
        io.header( Internal::kStateTag, Internal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 세션은 그래프 · 깃발 · 가방 · 상자를 빌린다 — 지역 사본에 읽으면 빌린 주소가 사본을 가리킨다. 새로 연 세션에 바로 읽고, 거절되면 `onStateRestored` 가 새 판을 연다.
        const bool bBound = _pCatalog != nullptr && _pGraphAsset != nullptr;
        if ( io.isReading() && ( bBound == false || openSession() == false ) )
        {
            SW_LOG_ERROR_ID( "SurvivalHorror.StateNeedsCatalog", "Survival horror game mode: state read before the catalog and the area graph are bound - refused" );
            io.fail();
            return;
        }
        io.value( _flags );
        io.value( _bag );
        io.value( _itemBox );
        vector<hashed_string> listVisited;
        vector<hashed_string> listDiscovered;
        _graph.fillVisitedAreas( listVisited );
        _graph.fillDiscoveredAreas( listDiscovered );
        io.list( listVisited );
        io.list( listDiscovered );
        io.value( _session ); // 자리 · 전투 자리 · 결과는 상태 칸이다(`GameModeComponent::serializeState`)
        io.expect( static_cast<uint32>( _listUsedSpot.size() ) );
        if ( io.isOk() == false )
            return;
        for ( uint8& bUsed : _listUsedSpot )
        {
            io.flag( bUsed );
        }
        io.value( _turn );
        io.value( _resultHold );
        if ( isFighting() )
        {
            // 전투는 같은 괴물 · 조사자로 연 뒤에 읽는다(키트 계약).
            const bool                            bKnownSpot = static_cast<size_t>( _encounterSpot ) < _stage._listSpot.size();
            const SurvivalHorrorMonsterDef* const pMonster   = bKnownSpot ? _pCatalog->findMonster( _stage._listSpot[static_cast<size_t>( _encounterSpot )]._id ) : nullptr;
            const bool                            bOpened    = pMonster != nullptr && ( io.isWriting() || _encounter.initialize( *pMonster, makeInvestigators(), _seed ) );
            if ( io.check( bOpened ) == false )
                return;
            io.value( _encounter );
        }
        io.check( _graph.findArea( _session.getCurrentArea() ) != nullptr );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _graph.restoreState( listVisited, listDiscovered );
    }
} // namespace sw
