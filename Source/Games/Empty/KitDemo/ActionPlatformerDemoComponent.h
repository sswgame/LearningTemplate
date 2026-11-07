/**
 * @file ActionPlatformerDemoComponent.h
 * @brief 장르 키트 시연 — 액션 플랫포머(`GF_ActionPlatformer`)의 스테이지 한 판입니다. 키트의 몸(`ActionPlatformerBody`)과 진행(`ActionStageRun`)을 플레이어 입력(입력 맵 `Move` · `KitDemo.Jump`)으로 돌립니다.
 * @details 지형은 씬 데이터(키트 글자 형식의 줄 — '#' 벽, '-' 발판, 'D' 흙, 'O' 갈고리 지점, 나머지는 빈 칸)이고, 체크포인트 · 비밀 · 골은 스테이지 정의(카탈로그)의 id 에 씬의 자리를 맞춘 것이다.
 *          아래로 떨어지면 죽고(목숨 하나) 지난 체크포인트에서 다시 선다. 골에 닿으면 깨고 결과(시간 · 피격 · 수집 점수와 등급)를 남긴다. 모습은 디버그 그리기다.
 *          장르 완결 루프 시나리오: `game/empty/automation/kitdemo_actionplatformer.scenario.xml`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionPlatformerBody.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionStageRun.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /**
     * @class ActionPlatformerDemoComponent
     * @brief 시작 → 체크포인트 · 비밀 → 골(또는 게임 오버)을 플레이어 입력으로 진행합니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Action Platformer Demo", Tooltip = "Runs one ActionPlatformer kit stage (checkpoints, secrets, goal, grade) from the player's input actions" )
    class ActionPlatformerDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        ActionPlatformerDemoComponent();
        virtual ~ActionPlatformerDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32   getRunState() const { return static_cast<int32>( _run.getState() ); }
        int32   getCheckpointIndex() const { return _run.getCheckpointIndex(); }
        int32   getSecretCount() const { return _run.getSecretCommittedCount() + _run.getSecretPendingCount(); }
        int32   getLives() const { return _run.getLives(); }
        float32 getScore() const { return _run.computeResult()._score; }
        float32 getPlayerX() const { return _body.getPosition()._x; }
        float32 getPlayerY() const { return _body.getPosition()._y; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 씬의 지형 줄로 칸 지형과 키트 격자를 칠합니다. 줄이 없거나 읽지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool buildTerrain();
        /** @brief 씬의 체크포인트 · 비밀 자리 수가 스테이지 정의의 id 수와 같은지 봅니다. 다르면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool checkMarkers( const ActionStageDef& stage ) const;
        /** @brief 몸을 @p position 에 멈춰 세웁니다(시작 · 되살아남). */
        void placeBody( const float2& position );
        /** @brief 몸의 자리로 체크포인트 · 비밀 · 낙사 · 골을 진행에 알립니다. */
        void applyMarkers();
        void drawStage() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Action platformer catalog XML (stages, grading, body settings)" )
        AssetRef<ActionPlatformerCatalog> _catalogPath;
        PROPERTY( Category = "Data", DisplayName = "Stage", Tooltip = "Stage id in the catalog" )
        hashed_string _stageID;
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

        shared_ptr<const ActionPlatformerCatalog> _pCatalog;
        PlatformTileMap                           _map;
        ActionTerrainGrid                         _terrain;
        ActionPlatformerBody                      _body;
        ActionStageRun                            _run;
        uint8                                     _bJumpHeldLastFrame : 1;
    };
} // namespace sw
