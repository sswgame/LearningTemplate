#include "pch.h"

#include "Games/Empty/KitDemo/ActionPlatformerDemoComponent.h"

#include "Core/String/FourCcUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct ActionPlatformerDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "KDAP" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kJumpAction = "KitDemo.Jump";

            /** @brief 칸 한 변(m)과 원점입니다. 씬의 자리(체크포인트 X · 비밀 · 골)가 이 격자의 월드 좌표다. */
            static constexpr float32 kTileSize = 1.0f;
        };
    } // namespace

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
        , _bJumpHeldLastFrame{ SW_FALSE }
    {
    }

    ActionPlatformerDemoComponent::~ActionPlatformerDemoComponent() = default;

    bool ActionPlatformerDemoComponent::buildTerrain()
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

    bool ActionPlatformerDemoComponent::checkMarkers( const ActionStageDef& stage ) const
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
        _body.initialize( PlatformerSettings{}, _pCatalog->getBodySettings() );
        _body.setPosition( position );
    }

    bool ActionPlatformerDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        if ( _run.start( _pCatalog.get(), _stageID ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.PlatformerUnknownStage", "Action platformer demo: stage '%#' is not in the catalog - the stage does not open", _stageID.c_str() );
            return false;
        }
        if ( checkMarkers( *_run.getStage() ) == false || buildTerrain() == false )
            return false;
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
        if ( nextCheckpoint < _listCheckpointX.size() && position._x >= _listCheckpointX[nextCheckpoint] && _run.reachCheckpoint( stage._listCheckpoint[nextCheckpoint] ) )
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
            const hashed_string respawn = _run.die();
            if ( _run.getState() == ActionStageState::GameOver )
            {
                SW_LOG_INFO_ID( "KitDemo.PlatformerGameOver", "[KitDemo] action platformer: game over" );
                return;
            }
            const int32  checkpointIndex = _run.getCheckpointIndex();
            const float2 respawnPosition = checkpointIndex >= 0 ? float2{ _listCheckpointX[static_cast<size_t>( checkpointIndex )], _startPosition._y } : _startPosition;
            placeBody( respawnPosition );
            SW_LOG_INFO_ID( "KitDemo.PlatformerDied", "[KitDemo] action platformer: fell - %# lives left, back at '%#'", _run.getLives(), respawn.empty() ? "start" : respawn.c_str() );
            return;
        }
        if ( position._x < _goalX || _run.clearStage() == false )
            return;
        const ActionStageResult result = _run.computeResult();
        SW_LOG_INFO_ID( "KitDemo.PlatformerCleared", "[KitDemo] action platformer: cleared in %# s - score %#, grade '%#', secrets %#/%#, deaths %#", result._clearTime, result._score,
                        result._grade.empty() ? "-" : result._grade.c_str(), result._secretFound, result._secretTotal, result._deathCount );
    }

    void ActionPlatformerDemoComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _run.getState() != ActionStageState::Playing )
        {
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

    void ActionPlatformerDemoComponent::serializeState( StateIO& io )
    {
        io.header( ActionPlatformerDemoComponentInternal::kStateTag, ActionPlatformerDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 지형 · 카탈로그는 씬 데이터에서 `startGame` 이 이미 세웠다. 몸과 진행만 싣는다(진행은 카탈로그를 빌려 id 로 스테이지를 찾는다).
        ActionPlatformerBody restoredBody;
        ActionStageRun       restoredRun;
        if ( io.isReading() )
        {
            restoredBody.initialize( PlatformerSettings{}, _pCatalog->getBodySettings() );
            restoredRun.bindCatalog( _pCatalog.get() );
        }
        io.value( io.isWriting() ? _body : restoredBody );
        io.value( io.isWriting() ? _run : restoredRun );
        uint8 bJumpHeldLastFrame = _bJumpHeldLastFrame;
        io.value( bJumpHeldLastFrame );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _body               = std::move( restoredBody );
        _run                = std::move( restoredRun );
        _bJumpHeldLastFrame = bJumpHeldLastFrame == SW_FALSE ? SW_FALSE : SW_TRUE;
    }
} // namespace sw
