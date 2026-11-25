/**
 * @file RTSCommanderAI.h
 * @brief 컴퓨터 상대를 구현합니다. 행동 트리로 일꾼, 보급, 생산 건물, 병력을 늘리고, 공격받으면 막고, 병력이 차면 쳐들어갑니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"

namespace sw
{
    /**
     * @struct RTSCommanderAISettings
     * @brief 컴퓨터 상대 하나의 성향(경제 · 병력 목표, 생각 주기, 짓는 자리)입니다.
     * @details 값은 데이터(디렉터의 PROPERTY)에 있고 코드에는 기본 성향이 없다. 적지 않은 칸은 0 이라 판을 열 때 거절한다(`isSet`).
     */
    REFLECT()
    struct SW_KIT_API RTSCommanderAISettings
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Seconds between two decisions" )
        float32 _thinkInterval{ 0.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "The army answers attacks only within this distance of the main base" )
        float32 _defendRadius{ 0.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Workers are sent to minerals within this distance of the main base" )
        float32 _resourceSearchRadius{ 0.0f };
        PROPERTY( Min = 0, Tooltip = "Workers the AI trains" )
        int32 _workerTarget{ 0 };
        PROPERTY( Min = 0, Tooltip = "Production buildings the AI builds" )
        int32 _productionTarget{ 0 };
        PROPERTY( Min = 0, Tooltip = "Army size that starts an attack" )
        int32 _attackArmySize{ 0 };
        PROPERTY( Min = 0, Tooltip = "The AI builds supply when the free supply is at most this" )
        int32 _supplyMargin{ 0 };
        PROPERTY( Min = 0, Tooltip = "Cells around the main base kept free when the AI looks for a building site" )
        int32 _buildSiteMinRadius{ 0 };
        PROPERTY( Min = 0, Tooltip = "Cells around the main base in which the AI looks for a building site" )
        int32 _buildSiteMaxRadius{ 0 };
        /** @brief 성향이 모두 적혔으면 true 입니다(보급 여유는 0 도 뜻이 있다). */
        bool isSet() const
        {
            const bool bTargets = _workerTarget >= 1 && _productionTarget >= 1 && _attackArmySize >= 1;
            const bool bRanges  = _thinkInterval > 0.0f && _defendRadius > 0.0f && _resourceSearchRadius > 0.0f;
            return bTargets && bRanges && _buildSiteMinRadius >= 1 && _buildSiteMaxRadius >= _buildSiteMinRadius;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 컴퓨터 상대가 짓고 만드는 유닛의 카탈로그 id 입니다. 값은 데이터(디렉터의 PROPERTY)에 있다. */
    REFLECT()
    struct SW_KIT_API RTSCommanderUnits
    {
        REFLECT_BODY();
        PROPERTY( Required, DisplayName = "Worker" )
        CatalogKey<RTSUnitDef> _workerID{};
        PROPERTY( Required, DisplayName = "Depot", Tooltip = "Resource drop-off building" )
        CatalogKey<RTSUnitDef> _depotID{}; ///< 일꾼을 만드는 본진
        PROPERTY( Required, DisplayName = "Supply", Tooltip = "Supply building" )
        CatalogKey<RTSUnitDef> _supplyID{};
        PROPERTY( Required, DisplayName = "Production", Tooltip = "Building that trains the army unit" )
        CatalogKey<RTSUnitDef> _productionID{};
        PROPERTY( Required, DisplayName = "Army", Tooltip = "Unit the AI trains and attacks with" )
        CatalogKey<RTSUnitDef> _armyUnitID{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class RTSCommanderAI
     * @brief 플레이어 한 명을 맡는 AI 입니다. 트리는 데이터(`*.bt.xml`, 게임 팩의 `commander.bt.xml`)이고 아래 모양이 기본 데이터다. 반응형 셀렉터라서 위쪽 노드를 먼저 평가합니다.
     *        잎이 부르는 작업은 이 클래스가 `rts.defend` 같은 이름으로 `ActionRegistry` 에 올린다(`SW_GAME_ACTION`) - 데이터가 순서 · 중단 규칙 · 가지를 정하고 코드는 작업만 가진다.
     * @code
     *     Root (reactive)
     *     ├─ [Threat IsSet, LowerPriority 중단] Defend: 병력을 위협 위치로 공격 이동
     *     └─ Economy (sequence, 모두 ForceSuccess)
     *        ├─ GatherIdle      : 노는 일꾼을 가까운 광물로 보낸다
     *        ├─ TrainWorkers    : 목표 수까지 본진에서 일꾼을 만든다
     *        ├─ BuildSupply     : 남은 보급이 적으면 보급 건물을 짓는다
     *        ├─ BuildProduction : 목표 수까지 생산 건물을 짓는다
     *        ├─ TrainArmy       : 노는 생산 건물마다 병력을 만든다
     *        └─ Attack          : 병력이 차면 적 시작 지점으로 공격 이동한다
     * @endcode
     * @details 월드의 알림을 `notify` 로 넘겨야 "공격받고 있다" 를 알 수 있다. 월드와 설정은 AI 보다 오래 살아야 한다.
     */
    REFLECT()
    class SW_KIT_API RTSCommanderAI
    {
    public:
        REFLECT_BODY();

        RTSCommanderAI();

        /**
         * @brief 월드 · 플레이어 · 설정으로 AI 를 시작합니다. 행동 트리는 데이터에서 읽은 것(`BehaviorTreeAsset::getTree`)을 복사해 쥔다 - 트리가 올바르지 않으면 어설트합니다(데이터 읽기가 이미 걸렀어야 한다).
         * @param tree 지휘관 트리입니다. 잎의 `task` 는 이 클래스가 등록한 `rts.*` 이름이어야 한다.
         */
        void initialize( RTSWorld* pWorld, int32 player, const RTSCommanderAISettings& settings, const RTSCommanderUnits& units, const BehaviorTree& tree );
        /** @brief `_thinkInterval` 마다 행동 트리를 한 번 실행합니다. */
        void update( float32 deltaTime );
        /** @brief 월드의 알림(공격받음)을 받습니다. */
        void notify( const RTSEvent& event );

        const utf8*       getActiveTaskName() const { return _runner.getActiveLeafName(); }
        int32             getPlayer() const { return _player; }
        int32             getAttackWaveCount() const { return _attackWaveCount; }
        const Blackboard& getBlackboard() const { return _blackboard; }
        /** @brief 행동 트리 실행기입니다(행동 트리 창의 활성 노드 표시가 읽는다). */
        const BehaviorTreeRunner& getRunner() const { return _runner; }

        /**
         * @brief 생각 간격 타이머와 공격 횟수를 씁니다. 행동 트리와 블랙보드는 쓰지 않으며, 읽은 쪽에서 트리를 처음부터 다시 고릅니다.
         *        `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO_IN_PLACE(); // 행동 트리 실행기는 복사할 수 없다 — 사본 대신 실패하면 읽기 전 값으로 되돌린다

        // 트리 데이터(`*.bt.xml`)가 이름으로 가리키는 작업입니다(`RTSCommanderAI.cpp` 끝의 `SW_GAME_ACTION` 줄이 `rts.*` 이름을 붙인다).
        static ActionResult taskDefend( ActionContext& context, const VocabularyNoParams& params );
        static ActionResult taskGatherIdle( ActionContext& context, const VocabularyNoParams& params );
        static ActionResult taskTrainWorkers( ActionContext& context, const VocabularyNoParams& params );
        static ActionResult taskBuildSupply( ActionContext& context, const VocabularyNoParams& params );
        static ActionResult taskBuildProduction( ActionContext& context, const VocabularyNoParams& params );
        static ActionResult taskTrainArmy( ActionContext& context, const VocabularyNoParams& params );
        static ActionResult taskAttack( ActionContext& context, const VocabularyNoParams& params );

    private:
        /** @brief 놀거나 채취 중인 일꾼 하나로 @p buildingID 를 본진 둘레에 짓습니다. */
        bool      orderConstruction( const hashed_string& buildingID );
        RTSUnitID findDepot() const;
        void      collectArmy( vector<RTSUnitID>& outListUnit ) const;

    private:
        BehaviorTree           _tree;
        BehaviorTreeRunner     _runner;
        Blackboard             _blackboard;
        RTSCommanderAISettings _settings;
        RTSCommanderUnits      _units;
        RTSWorld*              _pWorld;
        Countdown              _thinkTimer;
        int32                  _player;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _attackWaveCount;
    };
} // namespace sw
