#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct SkirmishGameModeComponentInternal
        {
            static constexpr FormatDesc kStateFormat{ "RTMS" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkirmishGameModeComponent::SkirmishGameModeComponent()
        : _commanderTreePath{}
        , _map{}
        , _matchSettings{}
        , _speedRange{}
        , _pCommanderTree{}
        , _commanderDebugSource{ *this }
        , _match{}
        , _listStepEvent{}
        , _speed{}
        , _matchID{ 0 }
        , _bAutoPlayChangeReported{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SkirmishGameModeComponent::~SkirmishGameModeComponent() = default;

    void SkirmishGameModeComponent::CommanderDebugSource::collectBehaviorTreeRunners( vector<BehaviorTreeDebugRunner>& outList ) const
    {
        for ( int32 player = 0; player < _owner._match.getPlayerCount(); ++player )
        {
            const RTSCommanderAI* pAI = _owner._match.findAI( player );
            if ( pAI == nullptr )
                continue;
            fixed_string<constant::buffer::kMax256> name;
            formatstring( name.data(), name.capacity(), "Player %#", player );
            BehaviorTreeDebugRunner& entry = outList.emplace_back();
            entry._name                    = name.view();
            entry._treePath                = _owner._commanderTreePath.path();
            entry._pRunner                 = &pAI->getRunner();
        }
    }

    SkirmishGameModeComponent* SkirmishGameModeComponent::findInScene( const GameObjectManager& manager )
    {
        return manager.getComponentRegistry().findUnique<SkirmishGameModeComponent>();
    }

    void SkirmishGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( SkirmishGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() && findCatalog() != nullptr )
            _match.getWorld().rebindCatalog( *findCatalog() ); // 다시 읽힌 카탈로그면 새 판으로 읽는다
        uint64 matchID = _matchID;
        io.value( matchID );
        io.value( _match );
        float32 timeScale = _speed.getTimeScale();
        uint8   bPaused   = _speed.isPaused() ? SW_TRUE : SW_FALSE;
        io.value( timeScale );
        io.value( bPaused );
        io.end();
        if ( io.isReading() && io.isOk() )
        {
            if ( io.check( _speed.restore( timeScale, bPaused != SW_FALSE ) && matchID != 0 ) == false )
                return; // 범위 밖 배속 · 정체 없는 판(깨진 세이브)은 거절한다
            _matchID = matchID;
            _listStepEvent.clear();
        }
    }

    void SkirmishGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
        {
            SW_LOG_INFO_ID( "SkirmishGameMode.MatchStateRestored", "[Skirmish] match state restored - %#s in", static_cast<int32>( _match.getWorld().getTime() ) );
        }
        else
        {
            SW_LOG_WARNING_ID( "SkirmishGameMode.SavedMatchStateDoes", "[Skirmish] the saved match state does not match this build - the current match goes on" );
        }
    }

    bool SkirmishGameModeComponent::startGame()
    {
        if ( acquireCatalog() == false )
            return false;
        if ( acquireRequired( _commanderTreePath, _pCommanderTree ) == false )
            return false;
        // AI · 맵 배치가 쓰는 유닛 id 는 유닛 카탈로그에 있어야 한다.
        const RTSCommanderUnits& units = _matchSettings._units;
        if ( hasUnits( { &units._workerID, &units._depotID, &units._supplyID, &units._productionID, &units._armyUnitID } ) == false )
            return false;
        if ( _speed.initialize( _speedRange ) == false )
            return false; // 까닭은 남았다
        const bool bAIStylesSet = _matchSettings._rushAI.isSet() && _matchSettings._macroAI.isSet() && _matchSettings._opponentAI.isSet();
        if ( bAIStylesSet == false )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.DirectorNeedsRushAIMacroAI", "[Skirmish] the game mode needs _matchSettings._rushAI, _macroAI and _opponentAI (every target, interval and radius) - the match cannot start" );
            return false;
        }
        TileMapXMLData tileMap;
        if ( tileMap.load( AssetID{ _map._tileMapPath } ) == false )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.MapMissing", "[Skirmish] the game mode needs a readable _map._tileMapPath (got '%#') - the match cannot start", _map._tileMapPath.c_str() );
            return false;
        }
        const optional<uint8> humanMask = openPlayers( isAutoPlayOn() );
        if ( humanMask.has_value() == false )
            return false; // 까닭은 대전을 여는 파생이 남겼다
        if ( _match.initialize( findCatalog(), _map, tileMap, *humanMask, _matchSettings, getStartMinerals(), _pCommanderTree.get() ) == false )
            return false;
        _listStepEvent.clear();
        do
        {
            _matchID = MathUtil::getRandom<uint64>();
        } while ( _matchID == 0 );
        return true;
    }

    void SkirmishGameModeComponent::updateGame( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        _listStepEvent.clear(); // pollEvents 는 뒤에 붙인다. 시간이 멈춘 걸음도 비워 클라이언트가 같은 알림을 두 번 받지 않는다
        if ( deltaTime <= 0.0f )
            return;

        // 자동 플레이는 판을 열 때 한 번 읽는다(누가 사람인지 판에 박힌다) — 판이 열린 뒤 바뀌면 조용히 무시하지 않고 알린다(시나리오는 <Launch> 로 켠다).
        if ( _bAutoPlayChangeReported == SW_FALSE && isAutoPlayOn() == _match.hasHumanPlayer() )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.AutoPlayChangedAfterStart", "[Skirmish] autoplay changed after the match opened - it is read once when the match opens (set the game's autoplay variable at launch)" );
            _bAutoPlayChangeReported = SW_TRUE;
        }
        stepMatch( frameTime );
        _match.pollEvents( _listStepEvent );
    }

    optional<uint8> SkirmishGameModeComponent::openPlayers( bool bAutoPlay )
    {
        return bAutoPlay ? uint8{ 0 } : SkirmishMatch::kHumanPlayer0;
    }

    void SkirmishGameModeComponent::stepMatch( GameSeconds frameTime )
    {
        if ( _speed.isPaused() == false )
            _match.update( frameTime, _speed.getTimeScale() );
    }

    int32 SkirmishGameModeComponent::submitCommand( RTSCommand&& command )
    {
        return isStarted() ? _match.getWorld().applyCommand( getLocalPlayer(), command ) : 0;
    }

    bool SkirmishGameModeComponent::submitTimeInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered )
    {
        if ( isStarted() == false )
            return false;
        _speed.applyInput( bPauseTriggered, bFasterTriggered, bSlowerTriggered );
        return true;
    }
} // namespace sw
