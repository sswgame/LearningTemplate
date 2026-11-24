#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct SkirmishGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "RTMS" );
            static constexpr uint32 kStateVersion = 3; ///< 3: 판의 정체(`getMatchID`)를 싣고, 고른 유닛 · 부대가 클라이언트 구간(`RTSCommandInputComponent`)으로 나갔다(옛 판은 거절한다 — 출시 전)
        };

        /** @brief 감시 값 `Skirmish.UnitCount` 입니다. 두 진영의 유닛 · 건물 수의 합입니다(AI 가 일꾼 · 군대를 늘리는지 본다). */
        [[nodiscard]] bool readUnitCount( const SkirmishGameModeComponent& gameMode, float64& outValue )
        {
            uint32 count = 0;
            gameMode.getWorld().forEachUnit( [&count]( const RTSUnit& unit )
            {
                (void)unit;
                ++count;
            } );
            outValue = static_cast<float64>( count );
            return true;
        }

        /** @brief 감시 값 `Skirmish.AIPlayerCount` 입니다. 컴퓨터가 맡은 플레이어(0 · 1) 수입니다 — 자동 플레이로 열렸으면 2 다. */
        [[nodiscard]] bool readAIPlayerCount( const SkirmishGameModeComponent& gameMode, float64& outValue )
        {
            const SkirmishMatch& match = gameMode.getMatch();
            outValue                   = static_cast<float64>( ( match.findAI( 0 ) != nullptr ? 1 : 0 ) + ( match.findAI( 1 ) != nullptr ? 1 : 0 ) );
            return true;
        }

        /** @brief 감시 값 `Skirmish.AITask` 입니다. 플레이어 `key`(0 · 1)를 맡은 컴퓨터 상대의 행동 트리가 도는 잎(`GatherIdle` · `TrainWorkers` · `Defend` …)입니다. */
        [[nodiscard]] bool readAITask( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const SkirmishGameModeComponent* pGameMode = query._pManager != nullptr ? SkirmishGameModeComponent::findInScene( *query._pManager ) : nullptr;
            int32                            player    = -1;
            if ( pGameMode == nullptr || StringUtil::parseInt( query._key, player ) == false )
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "key must be a player index, got '%#'", query._key );
                outValue.setText( pGameMode == nullptr ? string_view( "no skirmish game mode" ) : text.view() );
                return false;
            }
            const RTSCommanderAI* pAI = pGameMode->getMatch().findAI( player );
            if ( pAI == nullptr )
            {
                fixed_string<constant::buffer::kMax256> text;
                formatstring( text.data(), text.capacity(), "player %# is not played by the AI", query._key );
                outValue.setText( text.view() );
                return false;
            }
            const utf8* pTask = pAI->getActiveTaskName();
            outValue.setText( pTask != nullptr && pTask[0] != '\0' ? string_view{ pTask } : string_view{ "none" } );
            return true;
        }
    } // namespace

    namespace
    {
        /** @brief 개발 명령입니다. 활성 씬의 디렉터를 찾습니다. */
        struct SkirmishGameModeComponentDevInternal
        {
            [[nodiscard]] static SkirmishGameModeComponent* findDirector( string& outReply )
            {
                const SceneManager* const  pScenes   = game::findSubsystem<SceneManager>();
                const Scene* const         pScene    = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const   pManager  = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                SkirmishGameModeComponent* pDirector = pManager != nullptr ? SkirmishGameModeComponent::findInScene( *pManager ) : nullptr;
                if ( pDirector == nullptr )
                    outReply = "skirmish: no skirmish director in the active scene";
                return pDirector;
            }

            static bool runOrder( const vector<string>& listArgument, string& outReply )
            {
                float32 x = 0.0f;
                float32 z = 0.0f;
                if ( listArgument.size() != 2 || StringUtil::parseFloat( listArgument[0], x ) == false || StringUtil::parseFloat( listArgument[1], z ) == false )
                    return false;
                SkirmishGameModeComponent* const pDirector = findDirector( outReply );
                if ( pDirector == nullptr )
                    return true;
                const int32 player = pDirector->getLocalPlayer();
                RTSCommand  command;
                command._kind        = RTSCommandKind::GroupMove;
                command._target      = float3{ x, 0.0f, z };
                command._bAttackMove = SW_TRUE;
                pDirector->getWorld().forEachUnit( [player, &command]( const RTSUnit& unit )
                {
                    const bool bArmy = unit._owner == player && unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack();
                    if ( bArmy && static_cast<int32>( command._listUnit.size() ) < RTSCommand::kMaxUnitCount )
                        command._listUnit.push_back( unit._id );
                } );
                const uint32                             unitCount = static_cast<uint32>( command._listUnit.size() );
                const int32                              applied   = pDirector->submitCommand( std::move( command ) );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "skirmish: player %# attack-moves %# units - %#", player, unitCount, applied < 0 ? "sent with the next lockstep input" : "applied" );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishUnitCount, "Skirmish.UnitCount", "Units and buildings of both players", SkirmishGameModeComponent, &readUnitCount );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishAIPlayerCount, "Skirmish.AIPlayerCount", "Players (0 and 1) the AI commander plays - 2 when the match opened in autoplay", SkirmishGameModeComponent,
                                   &readAIPlayerCount );
    SW_DEV_COMMAND( SkirmishOrder, "skirmish.order", "skirmish.order <x> <z>", "Attack-move every army unit of this screen's player to the map point (through the lockstep input in a network match)",
                    &SkirmishGameModeComponentDevInternal::runOrder );

    SW_AUTOMATION_QUERY_WATCH( skirmishAITask, "Skirmish.AITask", "Active behavior tree task of the AI commander playing player key (0 or 1)", &readAITask, false, true );
} // namespace sw

namespace sw
{
    SkirmishGameModeComponent::SkirmishGameModeComponent()
        : _commanderTreePath{}
        , _huntIntervalSeconds{ 4.0f }
        , _arrivedRadius{ 10.0f }
        , _statusLogInterval{ 30.0f }
        , _workerUnitID{}
        , _depotUnitID{}
        , _supplyUnitID{}
        , _productionUnitID{}
        , _armyUnitID{}
        , _rushAI{}
        , _macroAI{}
        , _opponentAI{}
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
        for ( int32 player = 0; player < SkirmishMatch::kPlayerCount; ++player )
        {
            const RTSCommanderAI* pAI = _owner._match.findAI( player );
            if ( pAI == nullptr )
                continue;
            BehaviorTreeDebugRunner& entry = outList.emplace_back();
            entry._name                    = player == 0 ? "Player 0" : "Player 1"; // 판은 두 플레이어(kPlayerCount)
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
        io.header( SkirmishGameModeComponentInternal::kStateTag, SkirmishGameModeComponentInternal::kStateVersion );
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
            GameSpeedControl restoredSpeed;
            if ( io.check( restoredSpeed.restore( timeScale, bPaused != SW_FALSE ) && matchID != 0 ) == false )
                return; // 범위 밖 배속 · 정체 없는 판(깨진 세이브)은 거절한다
            _speed   = restoredSpeed;
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
            // 판이 반쯤 바뀌었을 수 있다. 새 판으로 되돌린다.
            SW_LOG_WARNING_ID( "SkirmishGameMode.SavedMatchStateDoes", "[Skirmish] the saved match state does not match this build - starting a new match" );
            (void)openGame();
        }
    }

    bool SkirmishGameModeComponent::startGame()
    {
        if ( acquireCatalog() == false )
            return false;
        if ( acquireRequired( _commanderTreePath, _pCommanderTree ) == false )
            return false;
        // AI · 맵 배치가 쓰는 유닛 id 는 유닛 카탈로그에 있어야 한다.
        if ( hasUnits( { &_workerUnitID, &_depotUnitID, &_supplyUnitID, &_productionUnitID, &_armyUnitID } ) == false )
            return false;
        const bool bAIStylesSet = _rushAI.isSet() && _macroAI.isSet() && _opponentAI.isSet();
        if ( bAIStylesSet == false )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.DirectorNeedsRushAIMacroAI", "[Skirmish] the game mode needs _rushAI, _macroAI and _opponentAI (worker, production and attack targets) - the match cannot start" );
            return false;
        }
        const optional<uint8> humanMask = openPlayers( isAutoPlayOn() );
        if ( humanMask.has_value() == false )
            return false; // 까닭은 대전을 여는 파생이 남겼다
        SkirmishMatchSettings matchSettings;
        matchSettings._huntIntervalSeconds   = _huntIntervalSeconds;
        matchSettings._arrivedRadius         = _arrivedRadius;
        matchSettings._startMinerals         = getStartMinerals();
        matchSettings._statusIntervalSeconds = _statusLogInterval;
        matchSettings._workerID              = _workerUnitID.key();
        matchSettings._depotID               = _depotUnitID.key();
        matchSettings._supplyID              = _supplyUnitID.key();
        matchSettings._productionID          = _productionUnitID.key();
        matchSettings._armyUnitID            = _armyUnitID.key();
        matchSettings._rushAI                = _rushAI;
        matchSettings._macroAI               = _macroAI;
        matchSettings._opponentAI            = _opponentAI;
        matchSettings._pCommanderTree        = _pCommanderTree.snapshot(); // 판은 받은 때의 트리로 끝까지 돈다
        if ( _match.initialize( findCatalog(), *humanMask, matchSettings ) == false )
            return false;
        _listStepEvent.clear();
        do
        {
            _matchID = MathUtil::getRandom<uint64>();
        } while ( _matchID == 0 );
        return true;
    }

    void SkirmishGameModeComponent::updateGame( float32 deltaTime )
    {
        _listStepEvent.clear(); // pollEvents 는 뒤에 붙인다. 시간이 멈춘 걸음도 비워 클라이언트가 같은 알림을 두 번 받지 않는다
        if ( deltaTime <= 0.0f )
            return;

        // 자동 플레이는 판을 열 때 한 번 읽는다(누가 사람인지 판에 박힌다) — 판이 열린 뒤 바뀌면 조용히 무시하지 않고 알린다(시나리오는 <Launch> 로 켠다).
        if ( _bAutoPlayChangeReported == SW_FALSE && isAutoPlayOn() == _match.hasHumanPlayer() )
        {
            SW_LOG_ERROR_ID( "SkirmishGameMode.AutoPlayChangedAfterStart", "[Skirmish] autoplay changed after the match opened - it is read once when the match opens (set the game's autoplay variable at launch)" );
            _bAutoPlayChangeReported = SW_TRUE;
        }
        stepMatch( deltaTime );
        _match.pollEvents( _listStepEvent );
    }

    optional<uint8> SkirmishGameModeComponent::openPlayers( bool bAutoPlay )
    {
        return bAutoPlay ? uint8{ 0 } : SkirmishMatch::kHumanPlayer0;
    }

    void SkirmishGameModeComponent::stepMatch( float32 deltaTime )
    {
        if ( _speed.isPaused() == false )
            _match.update( deltaTime, _speed.getTimeScale() );
    }

    void SkirmishGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 클라이언트 모듈(`RTSBattlefieldViewComponent`)이 세운다.
        (void)manager;
        (void)bRespawnViews;
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
