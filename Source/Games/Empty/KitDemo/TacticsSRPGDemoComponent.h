/**
 * @file TacticsSRPGDemoComponent.h
 * @brief 장르 키트 시연 — 택틱스 SRPG(`GF_TacticsSRPG`)의 전투 한 판입니다. 키트의 전장(`SRPGBattlefield`) · 전투(`SRPGCombat`) · 적 AI(`SRPGCommanderAI`)를
 *        플레이어 입력(입력 맵 `Move` 로 커서, `KitDemo.Confirm` 으로 고르기 · 이동 · 공격, `KitDemo.EndTurn` 으로 차례 끝)으로 돌립니다.
 * @details 페이즈 방식이다 — 아군 페이즈는 플레이어가, 적 · 제3세력 페이즈는 AI 가 한다. 적이 모두 쓰러지면 승리, 아군이 모두 쓰러지면 패배다.
 *          전장 크기 · 지형 · 유닛 배치는 씬 데이터, 지형 · 무기 · 기체 · 파일럿 수치는 카탈로그다. 모습은 디버그 그리기다.
 *          장르 완결 루프 시나리오: `game/empty/automation/kitdemo_tactics.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Catalog/SRPGCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGBattlefield.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCommanderAI.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/GrowthLog.h"

namespace sw
{
    /** @brief 전투 결과입니다(감시 값 `KitDemo.Tactics.Result` 의 수). */
    enum class TacticsDemoResult : uint8
    {
        Ongoing = 0,
        Victory,
        Defeat
    };
} // namespace sw

namespace sw
{
    /**
     * @class TacticsSRPGDemoComponent
     * @brief 배치 → 아군 페이즈(고르기 · 이동 · 공격) ↔ 적 페이즈(AI) → 승패를 진행합니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Tactics SRPG Demo", Tooltip = "Runs one TacticsSRPG kit battle (player phase from input, enemy phase by the commander AI) to victory or defeat" )
    class TacticsSRPGDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        TacticsSRPGDemoComponent();
        virtual ~TacticsSRPGDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getPhaseTeam() const { return static_cast<int32>( _field.getPhaseTeam() ); }
        int32 getTurn() const { return _field.getTurn(); }
        int32 getResult() const { return static_cast<int32>( _result ); }
        int32 getCursorX() const { return _cursor._x; }
        int32 getCursorY() const { return _cursor._y; }
        int32 getSelectedUnit() const { return _selectedUnit; }
        int32 getEnemiesAlive() const { return _field.countAlive( SRPGTeam::Enemy ); }
        int32 getPlayersAlive() const { return _field.countAlive( SRPGTeam::Player ); }
        /** @brief 성장 기록에 남은 레벨 오름 수입니다(감시 값 `KitDemo.Tactics.LevelUps`). */
        int32 getLevelUps() const { return static_cast<int32>( _growthLog.countKind( GrowthEventKind::LevelUp ) ); }
        /** @brief 전장 유닛의 지금 레벨 · 경험치와 최근 성장 기록을 씁니다(HUD 섹션 `tacticsgrowth` — 성장 디버거). */
        void writeGrowthDebugLines( DebugHUDSectionWriter& writer ) const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 씬 데이터로 전장을 열고 유닛을 놓습니다. 목록 길이가 다르거나 놓을 수 없는 유닛이 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openField( SRPGBattlefield& outField ) const;
        /** @brief `Move` 축을 한 번 기울일 때 커서를 한 칸 옮깁니다(놓았다가 다시 기울여야 다음 칸). */
        void moveCursor();
        /** @brief 커서 칸에 따라 고르기 · 이동 · 공격 · 대기를 합니다. */
        void confirmAtCursor();
        /** @brief 고른 유닛이 커서의 적을 칠 수 있는 첫 무기로 칩니다. 칠 수 없으면 false 입니다. */
        [[nodiscard]] bool attackAtCursor( int32 targetIndex );
        /** @brief 적 · 제3세력 페이즈를 AI 가 끝냅니다. 움직일 유닛이 없으면 페이즈를 넘깁니다. */
        void runAIPhase();
        /** @brief 이벤트를 로그로 내고 승패를 정합니다. */
        void settle();
        void drawField() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "SRPG catalog XML (terrain, weapons, units, pilots)" )
        AssetRef<SRPGCatalog> _catalogPath;
        PROPERTY( Category = "Field", DisplayName = "Width", Min = 1 )
        int32 _width;
        PROPERTY( Category = "Field", DisplayName = "Height", Min = 1 )
        int32 _height;
        PROPERTY( Category = "Field", DisplayName = "Terrain", Tooltip = "Terrain id every cell starts with" )
        hashed_string _defaultTerrain;
        PROPERTY( Category = "Field", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Units", DisplayName = "Player Units", Tooltip = "Unit ids (catalog) of the player team, one per cell" )
        vector<hashed_string> _listPlayerUnit;
        PROPERTY( Category = "Units", DisplayName = "Player Cells", Tooltip = "Cells (whole numbers) of the player units" )
        vector<float2> _listPlayerCell;
        PROPERTY( Category = "Units", DisplayName = "Enemy Units", Tooltip = "Unit ids (catalog) of the enemy team, one per cell" )
        vector<hashed_string> _listEnemyUnit;
        PROPERTY( Category = "Units", DisplayName = "Enemy Cells", Tooltip = "Cells (whole numbers) of the enemy units" )
        vector<float2> _listEnemyCell;
        PROPERTY( Category = "Units", DisplayName = "Pilot", Tooltip = "Pilot id (catalog) every unit flies with" )
        hashed_string _pilot;

        shared_ptr<const SRPGCatalog> _pCatalog;
        SRPGBattlefield               _field;
        SRPGCommanderAI               _ai;
        ConsumerLease                 _eventLease;
        vector<SRPGEvent>             _listEvent;
        int2                          _cursor;
        int2                          _lastStep; ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 칸을 옮긴다
        int32                         _selectedUnit;
        TacticsDemoResult             _result;
        GrowthLog                     _growthLog; ///< 레벨 오름과 그 출처(명중 · 격파) — 표시용이라 저장하지 않는다
    };
} // namespace sw
