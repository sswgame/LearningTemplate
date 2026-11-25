/**
 * @file ActionPlatformerGameModeComponent.h
 * @brief 액션 플랫포머 키트(`GF_ActionPlatformer`)의 디렉터 — 스테이지 한 판을 진행합니다. 키트의 몸(`ActionPlatformerBody`)과 진행(`ActionStageRun`)을 플레이어 입력 명령(`submitPlayerInput`)으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트)을 들고 씬과 잇는다. 입력 읽기 · 모습은 클라이언트 모듈(`GF_Client_ActionPlatformer` 의 `ActionPlatformerAvatarComponent`)이
 *          이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          지형은 씬 데이터(키트 글자 형식의 줄 — '#' 벽, '-' 발판, 'D' 흙, 'O' 갈고리 지점, 나머지는 빈 칸)이고, 체크포인트 · 비밀 · 골은 스테이지 정의(카탈로그)의 id 에 씬의 자리를 맞춘 것이다.
 *          아래로 떨어지거나(낙사) 개발 명령 `actionplatformer.kill` 로 죽으면(목숨 하나) 공통 부활 흐름(`RespawnFlow`)이 스테이지의 `<Respawn>` 규칙대로
 *          다시 세운다 — 마지막 체크포인트 · 제자리(낭떠러지면 가까운 설 자리, `ActionPlatformerRespawnWorld`) · 시작 지점. 골에 닿으면 깨고 결과(시간 · 피격 · 수집 점수와 등급)를 남긴다.
 *          장르 완결 루프 시나리오: `game/playground/automation/kitdemo_actionplatformer.scenario.xml`, 부활: `game/playground/automation/kitdemo_respawn.scenario.xml`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerBody.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionStageRun.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Respawn/RespawnFlow.h"

namespace sw
{
    /**
     * @class ActionPlatformerGameModeComponent
     * @brief 시작 → 체크포인트 · 비밀 → 골(또는 게임 오버)을 플레이어 입력 명령으로 진행합니다.
     */
    REFLECT( Category = "ActionPlatformer", DisplayName = "Action Platformer Game Mode", Tooltip = "Runs one ActionPlatformer stage (checkpoints, secrets, goal, grade) from player input commands" )
    class SW_KIT_API ActionPlatformerGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        ActionPlatformerGameModeComponent();
        virtual ~ActionPlatformerGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력(가로 이동 축, 점프 키를 누른 채인가)을 넘깁니다. 다음 규칙 걸음(`updateGame`)이 몸에 넣는다 — 점프 누름(눌린 순간)은 디렉터가 누른 채 깃발로 가린다.
         * @return 판이 열렸고 진행 중이라 입력을 받으면 true 입니다(죽어 있는 동안도 받는다 — 되살아난 뒤 첫 걸음이 쓴다). 아니면 받지 않고 false 입니다.
         */
        [[nodiscard]] bool submitPlayerInput( float32 moveX, bool bJumpHeld );
        /** @brief 플레이어를 그 자리에서 죽입니다(목숨 하나, 부활 흐름 시작). 진행 중이 아니거나 이미 죽어 있으면 false 입니다. 낙사와 개발 명령이 부릅니다. */
        [[nodiscard]] bool killPlayer();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 진행 중(깸 · 게임 오버 전)이라 입력 명령을 받으면 true 입니다. */
        bool                   isAcceptingInput() const;
        const PlatformTileMap& getTileMap() const { return _map; }
        const ActionStageRun&  getRun() const { return _run; }
        /** @brief 씬이 놓은 비밀 자리입니다(스테이지 정의의 비밀 순서). */
        const vector<float2>& getSecretPositions() const { return _listSecretPosition; }
        float32               getGoalX() const { return _goalX; }
        float2                getPlayerPosition() const { return _body.getPosition(); }
        int32                 getRunState() const { return static_cast<int32>( _run.getState() ); }
        int32                 getCheckpointIndex() const { return _run.getCheckpointIndex(); }
        int32                 getSecretCount() const { return _run.getSecretCommittedCount() + _run.getSecretPendingCount(); }
        int32                 getLives() const { return _run.getLives(); }
        float32               getScore() const { return _run.computeResult()._score; }
        float32               getPlayerX() const { return _body.getPosition()._x; }
        float32               getPlayerY() const { return _body.getPosition()._y; }
        int32                 getRespawnPhase() const { return static_cast<int32>( _respawn.getPhase() ); }
        int32                 getRespawnCount() const { return _respawnCount; }
        int32                 getLastRespawnPolicy() const { return _lastRespawnPolicy; }
        float32               getLastRespawnShift() const { return _lastRespawnShift; }
        int32                 getInPlaceUses() const { return _respawn.getPolicyUses( RespawnPolicyKind::InPlace ); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 씬의 지형 줄로 칸 지형과 키트 격자를 칠합니다. 줄이 없거나 읽지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool populateTerrain();
        /** @brief 씬의 체크포인트 · 비밀 자리 수가 스테이지 정의의 id 수와 같은지 봅니다. 다르면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateMarkers( const ActionStageDef& stage ) const;
        /** @brief 몸을 @p position 에 멈춰 세웁니다(시작 · 되살아남). */
        void placeBody( const float2& position );
        /** @brief 몸의 자리로 체크포인트 · 비밀 · 낙사 · 골을 진행에 알립니다. */
        void applyMarkers();
        /** @brief 죽어 있는 동안 부활 흐름을 돌리고, 스폰하면 몸을 그 자리에 세웁니다. */
        void updateRespawn( float32 deltaTime );

    private:
        PROPERTY( Category = "Data", DisplayName = "Stage", Tooltip = "Stage id in the catalog" )
        CatalogKey<ActionStageDef> _stageID;
        PROPERTY( Category = "Stage", DisplayName = "Terrain Rows", Tooltip = "Terrain text rows, top first ('#' wall, '-' one-way, 'D' dirt, 'O' grapple point, anything else empty)" )
        vector<string> _listTerrainRow;
        PROPERTY( Category = "Stage", DisplayName = "Checkpoint X", Tooltip = "World X of each stage checkpoint, in the stage's checkpoint order", Units = m )
        vector<float32> _listCheckpointX;
        PROPERTY( Category = "Stage", DisplayName = "Secret Positions", Tooltip = "World position of each stage secret, in the stage's secret order", Units = m )
        vector<float2> _listSecretPosition;
        PROPERTY( Category = "Stage", DisplayName = "Start", Units = m )
        float2 _startPosition;
        PROPERTY( Category = "Stage", DisplayName = "Goal X", Units = m )
        float32 _goalX;
        PROPERTY( Category = "Stage", DisplayName = "Fall Y", Tooltip = "Below this world Y the player dies", Units = m )
        float32 _fallY;
        PROPERTY( Category = "Stage", DisplayName = "Pickup Radius", Min = 0.1, Units = m )
        float32 _pickupRadius;

        SharedAssetRef<ActionPlatformerCatalog> _pCatalog;
        PlatformTileMap                         _map;
        ActionTerrainGrid                       _terrain;
        ActionPlatformerBody                    _body;
        ActionStageRun                          _run;
        RespawnFlow                             _respawn;        ///< 규칙은 스테이지의 `<Respawn>`
        vector<RespawnSpawnPoint>               _listSpawnPoint; ///< 시작 지점 하나(`start`)
        PROPERTY( RuntimeState )
        int32 _respawnCount;
        PROPERTY( RuntimeState )
        int32 _lastRespawnPolicy; ///< 마지막 부활의 정책(`RespawnPolicyKind`, −1 이면 아직 없음)
        PROPERTY( RuntimeState )
        float32 _lastRespawnShift; ///< 마지막 부활에서 죽은 자리와 선 자리의 거리(m)
        float32 _commandMoveX;     ///< 마지막 입력 명령의 가로 이동 축 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        PROPERTY( RuntimeState )
        uint8                  _bJumpHeldLastFrame : 1;
        uint8                  _bCommandJumpHeld   : 1; ///< 마지막 입력 명령의 점프 키 누른 채(싣지 않는다 — `_commandMoveX` 와 같은 까닭)
        [[maybe_unused]] uint8 _reserved           : 6;
    };
} // namespace sw
