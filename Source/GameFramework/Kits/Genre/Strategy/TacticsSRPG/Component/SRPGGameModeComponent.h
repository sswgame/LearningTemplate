/**
 * @file SRPGGameModeComponent.h
 * @brief 택틱스 SRPG 키트(`GF_TacticsSRPG`)의 디렉터 — 전투 한 판을 진행합니다. 키트의 전장(`SRPGBattlefield`) · 전투(`SRPGCombat`) · 적 AI(`SRPGCommanderAI`)를
 *        씬 데이터로 열고, 아군 페이즈는 명령(`confirmCell` · `endPlayerPhase`)으로, 적 · 제3세력 페이즈는 AI 로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 커서 · 모습은 클라이언트 모듈
 *          (`GF_Client_TacticsSRPG` 의 `SRPGCursorComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          적이 모두 쓰러지면 승리, 아군이 모두 쓰러지면 패배다. 전장 크기 · 지형 · 유닛 배치는 씬 데이터, 수치는 카탈로그다.
 *          장르 완결 루프 시나리오: `game/playground/automation/kitdemo_tactics.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGBattlefield.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGCommanderAI.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/GrowthLog.h"

namespace sw
{
    /** @brief 전투 결과입니다(감시 값 `SRPG.Result` 의 수). */
    ENUM()
    enum class SRPGBattleResult : uint8
    {
        Ongoing = 0,
        Victory,
        Defeat
    };
} // namespace sw

namespace sw
{
    /**
     * @class SRPGGameModeComponent
     * @brief 배치 → 아군 페이즈(명령: 고르기 · 이동 · 공격 · 대기 · 페이즈 끝) ↔ 적 페이즈(AI) → 승패를 진행합니다.
     */
    REFLECT( Category = "TacticsSRPG", DisplayName = "SRPG Game Mode", Tooltip = "Runs one TacticsSRPG battle: player phase from commands, enemy phase by the commander AI, to victory or defeat" )
    class SW_KIT_API SRPGGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        SRPGGameModeComponent();
        virtual ~SRPGGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(아군 페이즈에만 받는다, 게임 스레드) ----
        /**
         * @brief 칸 @p cell 을 확정합니다. 고른 유닛이 없으면 그 칸의 아군을 고르고, 고른 유닛의 칸이면 대기(행동 끝), 적이면 닿는 첫 무기로 공격, 빈 칸이면 이동합니다.
         * @return 판이 바뀌었으면(고르기 · 대기 · 공격 · 이동) true 입니다. 명령을 받지 않는 때(`isAwaitingCommand`)이거나 할 수 없는 일이면 false 입니다.
         */
        [[nodiscard]] bool confirmCell( int2 cell );
        /** @brief 아군 페이즈를 끝냅니다(고른 것을 놓는다). 명령을 받지 않는 때면 false 입니다. */
        [[nodiscard]] bool endPlayerPhase();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const SRPGBattlefield& getField() const { return _field; }
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const SRPGCatalog* findCatalog() const { return _pCatalog.get(); }
        const GrowthLog&   getGrowthLog() const { return _growthLog; }
        SRPGBattleResult   getBattleResult() const { return _result; }
        /** @brief 판이 열렸고 진행 중이며 아군 페이즈라 명령을 받으면 true 입니다. */
        bool  isAwaitingCommand() const;
        int32 getPhaseTeam() const { return static_cast<int32>( _field.getPhaseTeam() ); }
        int32 getTurn() const { return _field.getTurn(); }
        int32 getResult() const { return static_cast<int32>( _result ); }
        int32 getSelectedUnit() const { return _selectedUnit; }
        int32 getEnemiesAlive() const { return _field.countAlive( SRPGTeam::Enemy ); }
        int32 getPlayersAlive() const { return _field.countAlive( SRPGTeam::Player ); }
        /** @brief 성장 기록에 남은 레벨 오름 수입니다(감시 값 `SRPG.LevelUps`). */
        int32 getLevelUps() const { return static_cast<int32>( _growthLog.countKind( GrowthEventKind::LevelUp ) ); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 씬 데이터로 전장을 열고 유닛을 놓습니다. 목록 길이가 다르거나 놓을 수 없는 유닛이 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openField( SRPGBattlefield& outField ) const;
        /** @brief 고른 유닛이 @p cell 의 적(@p targetIndex)을 칠 수 있는 첫 무기로 칩니다. 칠 수 없으면 false 입니다. */
        [[nodiscard]] bool attackCell( int2 cell, int32 targetIndex );
        /** @brief 적 · 제3세력 페이즈를 AI 가 끝냅니다. 움직일 유닛이 없으면 페이즈를 넘깁니다. */
        void runAIPhase();
        /** @brief 이벤트를 로그로 내고 승패를 정합니다. */
        void settle();

    private:
        PROPERTY( Category = "Field", DisplayName = "Width", Min = 1 )
        int32 _width;
        PROPERTY( Category = "Field", DisplayName = "Height", Min = 1 )
        int32 _height;
        PROPERTY( Category = "Field", DisplayName = "Terrain", Tooltip = "Terrain id every cell starts with" )
        CatalogKey<SRPGTerrainDef> _defaultTerrain;
        PROPERTY( Category = "Field", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Units", DisplayName = "Player Units", Tooltip = "Unit ids (catalog) of the player team, one per cell" )
        vector<CatalogKey<SRPGUnitDef>> _listPlayerUnit;
        PROPERTY( Category = "Units", DisplayName = "Player Cells", Tooltip = "Cells of the player units" )
        vector<int2> _listPlayerCell;
        PROPERTY( Category = "Units", DisplayName = "Enemy Units", Tooltip = "Unit ids (catalog) of the enemy team, one per cell" )
        vector<CatalogKey<SRPGUnitDef>> _listEnemyUnit;
        PROPERTY( Category = "Units", DisplayName = "Enemy Cells", Tooltip = "Cells of the enemy units" )
        vector<int2> _listEnemyCell;
        PROPERTY( Category = "Units", DisplayName = "Pilot", Tooltip = "Pilot id (catalog) every unit flies with" )
        CatalogKey<SRPGPilotDef> _pilot;

        SharedAssetRef<SRPGCatalog> _pCatalog;
        SRPGBattlefield             _field;
        SRPGCommanderAI             _ai;
        ConsumerLease               _eventLease;
        vector<SRPGEvent>           _listEvent;
        PROPERTY( RuntimeState, Min = -1 )
        int32 _selectedUnit;
        PROPERTY( RuntimeState )
        SRPGBattleResult _result;
        GrowthLog        _growthLog; ///< 레벨 오름과 그 출처(명중 · 격파) — 표시용이라 저장하지 않는다
    };
} // namespace sw
