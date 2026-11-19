#include "pch.h"

#include "Games/Empty/KitDemo/ActionPlatformerDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerRespawnWorld.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct ActionPlatformerDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDAP" );
            static constexpr uint32 kStateVersion = 2; ///< 2: 부활 흐름(`RespawnFlow`)을 싣는다

            static constexpr const utf8* kStartPointID = "start";

            static constexpr const utf8* kJumpAction = "KitDemo.Jump";

            /** @brief 칸 한 변(m)과 원점입니다. 씬의 자리(체크포인트 X · 비밀 · 골)가 이 격자의 월드 좌표다. */
            static constexpr float32 kTileSize = 1.0f;

            /** @brief 활성 씬의 플랫포머 시연입니다. 없으면 nullptr 입니다(개발 명령이 그 까닭으로 답한다). */
            [[nodiscard]] static ActionPlatformerDemoComponent* findDemo()
            {
                const SceneManager* const      pScenes  = game::findSubsystem<SceneManager>();
                const Scene* const             pScene   = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const       pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                ActionPlatformerDemoComponent* pDemo    = nullptr;
                if ( pManager != nullptr )
                {
                    for ( ActionPlatformerDemoComponent* pFound : pManager->getComponentRegistry().getAll<ActionPlatformerDemoComponent>() )
                    {
                        if ( pDemo == nullptr )
                            pDemo = pFound;
                    }
                }
                return pDemo;
            }

            static bool runKill( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                ActionPlatformerDemoComponent* const pDemo = findDemo();
                if ( pDemo == nullptr )
                    outReply = "kitdemo.platformer.kill: no action platformer demo in the active scene";
                else if ( pDemo->killPlayer() )
                    outReply = "kitdemo.platformer.kill: the player died where it stood";
                else
                    outReply = "kitdemo.platformer.kill: the run is not playing or the player is already dead";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( KitDemoPlatformerKill, "kitdemo.platformer.kill", "kitdemo.platformer.kill", "Kill the action platformer demo player where it stands (one life, then the stage's respawn rules)",
                    &ActionPlatformerDemoComponentInternal::runKill );

    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerState, "KitDemo.Platformer.State", "Action platformer demo run state (0 not started, 1 playing, 2 cleared, 3 game over)",
                                ActionPlatformerDemoComponent, &ActionPlatformerDemoComponent::getRunState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerCheckpoint, "KitDemo.Platformer.Checkpoint", "Last checkpoint index passed (-1 = the stage start)", ActionPlatformerDemoComponent,
                                &ActionPlatformerDemoComponent::getCheckpointIndex );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerSecrets, "KitDemo.Platformer.Secrets", "Secrets picked up (committed and carried)", ActionPlatformerDemoComponent,
                                &ActionPlatformerDemoComponent::getSecretCount );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerLives, "KitDemo.Platformer.Lives", "Lives left", ActionPlatformerDemoComponent, &ActionPlatformerDemoComponent::getLives );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerScore, "KitDemo.Platformer.Score", "Run score 0..100 (time, hits, secrets)", ActionPlatformerDemoComponent,
                                &ActionPlatformerDemoComponent::getScore );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerPlayerX, "KitDemo.Platformer.PlayerX", "Player body world X", ActionPlatformerDemoComponent, &ActionPlatformerDemoComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerPlayerY, "KitDemo.Platformer.PlayerY", "Player body world Y", ActionPlatformerDemoComponent, &ActionPlatformerDemoComponent::getPlayerY );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerRespawnPhase, "KitDemo.Platformer.RespawnPhase", "Respawn flow phase (0 alive, 1 waiting, 2 selection, 3 search)", ActionPlatformerDemoComponent,
                                &ActionPlatformerDemoComponent::getRespawnPhase );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerRespawns, "KitDemo.Platformer.Respawns", "Times the player stood up again", ActionPlatformerDemoComponent, &ActionPlatformerDemoComponent::getRespawnCount );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerLastRespawnPolicy, "KitDemo.Platformer.LastRespawnPolicy",
                                "Policy of the last respawn (0 last checkpoint, 1 in place, 2 nearest spawn point, 3 team base, 4 player choice, -1 none)", ActionPlatformerDemoComponent,
                                &ActionPlatformerDemoComponent::getLastRespawnPolicy );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerLastRespawnShift, "KitDemo.Platformer.LastRespawnShift", "Distance between where the player died and where it stood up last time (m)",
                                ActionPlatformerDemoComponent, &ActionPlatformerDemoComponent::getLastRespawnShift );
    SW_AUTOMATION_GETTER_WATCH( kitDemoPlatformerInPlaceUses, "KitDemo.Platformer.InPlaceUses", "Respawns used by the in-place policy", ActionPlatformerDemoComponent,
                                &ActionPlatformerDemoComponent::getInPlaceUses );

    ActionPlatformerDemoComponent::ActionPlatformerDemoComponent()
        : _catalogPath{}
        , _stageID{}
        , _listTerrainRow{}
        , _listCheckpointX{}
        , _listSecretPosition{}
        , _startPosition{ 1.5f, 1.6f }
        , _goalX{ 0.0f }
        , _fallY{ -3.0f }
        , _pickupRadius{ 0.6f }
        , _pCatalog{}
        , _map{}
        , _terrain{}
        , _body{}
        , _run{}
        , _respawn{}
        , _listSpawnPoint{}
        , _respawnCount{ 0 }
        , _lastRespawnPolicy{ constant::invalid_index::kInt32 }
        , _lastRespawnShift{ 0.0f }
        , _bJumpHeldLastFrame{ SW_FALSE }
    {
    }

    ActionPlatformerDemoComponent::~ActionPlatformerDemoComponent() = default;

    bool ActionPlatformerDemoComponent::populateTerrain()
    {
        if ( _listTerrainRow.empty() )
        {
            SW_LOG_ERROR_ID( "KitDemo.PlatformerNoTerrain", "Action platformer demo: the scene has no terrain rows - the stage does not open" );
            return false;
        }
        string text;
        for ( const string& row : _listTerrainRow )
        {
            text += row;
            text += '\n';
        }
        // 키트 격자가 칸 지형도 함께 칠한다('D' 흙은 지형에서 벽, 'O' 는 빈 칸 가운데의 갈고리 지점).
        if ( _terrain.loadFromXMLText( text, ActionPlatformerDemoComponentInternal::kTileSize, float2{ 0.0f, 0.0f }, _map ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.PlatformerTerrainUnreadable", "Action platformer demo: the terrain rows could not be read - the stage does not open" );
            return false;
        }
        return true;
    }

    bool ActionPlatformerDemoComponent::validateMarkers( const ActionStageDef& stage ) const
    {
        const bool bCheckpointsMatch = _listCheckpointX.size() == stage._listCheckpoint.size();
        const bool bSecretsMatch     = _listSecretPosition.size() == stage._listSecret.size();
        if ( bCheckpointsMatch && bSecretsMatch )
            return true;
        SW_LOG_ERROR_ID( "KitDemo.PlatformerMarkerMismatch", "Action platformer demo: stage '%#' has %# checkpoints and %# secrets but the scene places %# and %# - the stage does not open",
                         stage._id.c_str(), stage._listCheckpoint.size(), stage._listSecret.size(), _listCheckpointX.size(), _listSecretPosition.size() );
        return false;
    }

    void ActionPlatformerDemoComponent::placeBody( const float2& position )
    {
        _body.initialize( *_pCatalog );
        _body.setPosition( position );
    }

    bool ActionPlatformerDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        if ( _run.start( _pCatalog.get(), _stageID.key() ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.PlatformerUnknownStage", "Action platformer demo: stage '%#' is not in the catalog - the stage does not open", _stageID.c_str() );
            return false;
        }
        if ( validateMarkers( *_run.getStage() ) == false || populateTerrain() == false )
            return false;
        // 부활 규칙은 스테이지 데이터다 — 적지 않은 스테이지는 대신 정한 규칙으로 돌지 않고 열리지 않는다.
        if ( _respawn.initialize( _run.getStage()->_respawn ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.PlatformerNoRespawnRules", "Action platformer demo: stage '%#' has no valid <Respawn> rules - the stage does not open", _stageID.c_str() );
            return false;
        }
        RespawnSpawnPoint startPoint;
        startPoint._id       = hashed_string( ActionPlatformerDemoComponentInternal::kStartPointID );
        startPoint._position = float3{ _startPosition._x, _startPosition._y, 0.0f };
        _listSpawnPoint.clear();
        _listSpawnPoint.push_back( startPoint );
        _respawnCount      = 0;
        _lastRespawnPolicy = constant::invalid_index::kInt32;
        _lastRespawnShift  = 0.0f;
        placeBody( _startPosition );
        _bJumpHeldLastFrame = SW_FALSE;
        return true;
    }

    void ActionPlatformerDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.PlatformerStarted", "[KitDemo] action platformer: stage '%#' with %# lives - Move to run, KitDemo.Jump to jump", _stageID.c_str(), _run.getLives() );
    }

    void ActionPlatformerDemoComponent::applyMarkers()
    {
        const ActionStageDef& stage    = *_run.getStage();
        const float2          position = _body.getPosition();
        // 체크포인트는 순서대로만 지난다(키트가 앞쪽으로 되돌아가지 않게 막는다) — 지금 지난 다음 것만 본다.
        const size_t nextCheckpoint = static_cast<size_t>( _run.getCheckpointIndex() + 1 );
        // 체크포인트의 부활 자리는 그 X 와 시작 높이다(내려앉는다).
        const bool bCheckpointReached = nextCheckpoint < _listCheckpointX.size() && position._x >= _listCheckpointX[nextCheckpoint] &&
                                        _run.reachCheckpoint( stage._listCheckpoint[nextCheckpoint], float3{ _listCheckpointX[nextCheckpoint], _startPosition._y, 0.0f } );
        if ( bCheckpointReached )
            SW_LOG_INFO_ID( "KitDemo.PlatformerCheckpoint", "[KitDemo] action platformer: checkpoint '%#'", stage._listCheckpoint[nextCheckpoint].c_str() );
        for ( size_t secretIndex = 0; secretIndex < _listSecretPosition.size(); ++secretIndex )
        {
            const float2 delta     = position - _listSecretPosition[secretIndex];
            const bool   bInReach  = delta._x * delta._x + delta._y * delta._y <= _pickupRadius * _pickupRadius;
            const bool   bPickedUp = bInReach && _run.collectSecret( stage._listSecret[secretIndex] );
            if ( bPickedUp )
                SW_LOG_INFO_ID( "KitDemo.PlatformerSecret", "[KitDemo] action platformer: secret '%#' picked up", stage._listSecret[secretIndex].c_str() );
        }
        if ( position._y < _fallY )
        {
            (void)killPlayer();
            return;
        }
        if ( position._x < _goalX || _run.clearStage() == false )
            return;
        const ActionStageResult result = _run.computeResult();
        SW_LOG_INFO_ID( "KitDemo.PlatformerCleared", "[KitDemo] action platformer: cleared in %# s - score %#, grade '%#', secrets %#/%#, deaths %#", result._clearTime, result._score,
                        result._grade.empty() ? "-" : result._grade.c_str(), result._secretFound, result._secretTotal, result._deathCount );
    }

    bool ActionPlatformerDemoComponent::killPlayer()
    {
        if ( _run.getState() != ActionStageState::Playing || _respawn.isDead() )
            return false;
        const float2        position   = _body.getPosition();
        const hashed_string checkpoint = _run.die();
        if ( _run.getState() == ActionStageState::GameOver )
        {
            SW_LOG_INFO_ID( "KitDemo.PlatformerGameOver", "[KitDemo] action platformer: game over" );
            return true;
        }
        (void)_respawn.notifyDied( float3{ position._x, position._y, 0.0f }, 0.0f );
        SW_LOG_INFO_ID( "KitDemo.PlatformerDied", "[KitDemo] action platformer: died at (%#, %#) - %# lives left, last checkpoint '%#'", position._x, position._y, _run.getLives(),
                        checkpoint.empty() ? "start" : checkpoint.c_str() );
        return true;
    }

    void ActionPlatformerDemoComponent::updateRespawn( float32 deltaTime )
    {
        const ActionPlatformerRespawnWorld world( _map, _pCatalog->getMotorSettings()._halfExtents );
        RespawnContext                     context;
        context._pCheckpoints                = &_run.getCheckpoints();
        context._pSpawnPoints                = &_listSpawnPoint;
        context._pWorld                      = &world;
        const float3                 death   = _respawn.getDeathPosition();
        const optional<RespawnSpawn> respawn = _respawn.update( deltaTime, context );
        if ( respawn.has_value() == false )
            return;
        placeBody( float2{ respawn->_position._x, respawn->_position._y } );
        ++_respawnCount;
        _lastRespawnPolicy = static_cast<int32>( respawn->_policy );
        _lastRespawnShift  = float3::getDistance( respawn->_position, death );
        SW_LOG_INFO_ID( "KitDemo.PlatformerRespawned", "[KitDemo] action platformer: stood up by policy %# at (%#, %#) at '%#'", _lastRespawnPolicy, respawn->_position._x, respawn->_position._y,
                        respawn->_pointID.empty() ? "-" : respawn->_pointID.c_str() );
    }

    void ActionPlatformerDemoComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _run.getState() != ActionStageState::Playing )
        {
            drawStage();
            return;
        }
        // 죽어 있는 동안 몸은 멈춘다 — 흐름이 다시 세울 때까지 입력 · 몸을 진행하지 않는다.
        if ( _respawn.isDead() )
        {
            updateRespawn( deltaTime );
            drawStage();
            return;
        }
        // 이름은 한 번만 만든다(프레임마다 intern 하지 않는다).
        static const hashed_string s_moveAction( CommonInputActions::kMove );
        static const hashed_string s_jumpAction( ActionPlatformerDemoComponentInternal::kJumpAction );
        const bool                 bJumpHeld = isActionDown( s_jumpAction );

        ActionBodyInput input;
        input._motor._move         = float2{ getActionVector2D( s_moveAction )._x, 0.0f };
        input._motor._bJumpHeld    = bJumpHeld ? SW_TRUE : SW_FALSE;
        input._motor._bJumpPressed = bJumpHeld && _bJumpHeldLastFrame == SW_FALSE ? SW_TRUE : SW_FALSE;
        _bJumpHeldLastFrame        = bJumpHeld ? SW_TRUE : SW_FALSE;

        _body.update( _map, _terrain, input, deltaTime );
        _run.update( deltaTime );
        applyMarkers();
        drawStage();
    }

    void ActionPlatformerDemoComponent::drawStage() const
    {
        DebugDrawQueue* pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr )
            return;
        // 화면은 x 오른쪽 · y 위 평면이다(카메라가 -Z 에서 본다).
        const float32 half = ActionPlatformerDemoComponentInternal::kTileSize * 0.5f;
        for ( int32 y = 0; y < _map.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _map.getWidth(); ++x )
            {
                if ( _map.getTile( x, y ) != PlatformTile::Solid )
                    continue;
                pDraw->drawBox( float3( _map.getTileLeft( x ) + half, _map.getTileBottom( y ) + half, 0.0f ), float3( half, half, half ), float4( 0.45f, 0.4f, 0.35f, 1.0f ) );
            }
        }
        const ActionStageDef* pStage = _run.getStage();
        for ( size_t secretIndex = 0; pStage != nullptr && secretIndex < _listSecretPosition.size(); ++secretIndex )
        {
            if ( _run.isSecretFound( pStage->_listSecret[secretIndex] ) )
                continue;
            pDraw->drawSphere( float3( _listSecretPosition[secretIndex]._x, _listSecretPosition[secretIndex]._y, 0.0f ), 0.25f, float4( 1.0f, 0.85f, 0.2f, 1.0f ) );
        }
        const float2 position = _body.getPosition();
        pDraw->drawBox( float3( position._x, position._y, 0.0f ), float3( 0.35f, 0.45f, 0.35f ), float4( 0.2f, 0.6f, 1.0f, 1.0f ) );
        pDraw->drawLine( float3( _goalX, 0.0f, 0.0f ), float3( _goalX, 4.0f, 0.0f ), float4( 0.3f, 1.0f, 0.4f, 1.0f ) );
    }

    void ActionPlatformerDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 디버그 그리기뿐이라 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void ActionPlatformerDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( ActionPlatformerDemoComponentInternal::kStateTag, ActionPlatformerDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 지형 · 카탈로그는 씬 데이터에서 `startGame` 이 이미 세웠다. 몸과 진행만 싣는다(진행은 카탈로그를 빌려 id 로 스테이지를 찾는다).
        // 부활 흐름은 같은 스테이지 규칙으로 연 사본에 읽는다(규칙은 상태 바이트에 없다 — 스테이지는 `startGame` 이 세웠다).
        ActionPlatformerBody restoredBody;
        ActionStageRun       restoredRun;
        RespawnFlow          restoredRespawn;
        if ( io.isReading() )
        {
            restoredBody.initialize( *_pCatalog );
            restoredRun.bindCatalog( _pCatalog.get() );
            if ( io.check( _run.getStage() != nullptr && restoredRespawn.initialize( _run.getStage()->_respawn ) ) == false )
                return;
        }
        io.value( io.isWriting() ? _body : restoredBody );
        io.value( io.isWriting() ? _run : restoredRun ); // 점프 키 깃발 · 부활 셈은 상태 칸이다(`GameModeComponent::serializeState`)
        io.value( io.isWriting() ? _respawn : restoredRespawn );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _body    = std::move( restoredBody );
        _run     = std::move( restoredRun );
        _respawn = std::move( restoredRespawn );
    }
} // namespace sw
