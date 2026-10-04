/**
 * @file RtsAiController.h
 * @brief 컴퓨터 상대 — 행동 트리로 일꾼 · 보급 · 생산 건물 · 병력을 늘리고, 공격받으면 막고, 병력이 차면 쳐들어갑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/AI/BehaviorTree.h"
#include "GameFramework/AI/Blackboard.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsWorld.h"

namespace sw
{
    /** @brief AI 가 쓰는 정의 id 와 목표 수입니다(종족 · 난이도마다 데이터로 바꾼다). */
    struct RtsAiSettings
    {
        hashed_string _workerId{};
        hashed_string _depotId{};      ///< 본진(일꾼을 만든다)
        hashed_string _supplyId{};     ///< 보급 건물
        hashed_string _productionId{}; ///< 병력 건물
        hashed_string _armyUnitId{};   ///< 병력 유닛
        float32       _thinkInterval{ 0.5f };
        float32       _defendRadius{ 14.0f }; ///< 본진에서 이 안의 공격만 막으러 간다
        int32         _workerTarget{ 14 };
        int32         _productionTarget{ 2 };
        int32         _attackArmySize{ 8 };
        int32         _supplyMargin{ 3 }; ///< 남은 보급이 이만큼 이하면 보급 건물을 짓는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RtsAiController
     * @brief 한 플레이어를 맡는 AI 입니다. 트리는 이렇습니다(반응형 셀렉터 — 위가 먼저).
     * @code
     *     Root (reactive)
     *     ├─ [Threat IsSet, LowerPriority 중단] Defend — 병력을 위협 자리로 공격 이동
     *     └─ Economy (sequence, 모두 ForceSuccess)
     *        ├─ GatherIdle      — 노는 일꾼을 가까운 광물로
     *        ├─ TrainWorkers    — 목표 수까지 본진에서
     *        ├─ BuildSupply     — 남은 보급이 적으면
     *        ├─ BuildProduction — 목표 수까지
     *        ├─ TrainArmy       — 노는 생산 건물마다
     *        └─ Attack          — 병력이 차면 적 시작 지점으로 공격 이동
     * @endcode
     *          월드의 알림을 `notify` 로 넘겨야 "공격받고 있다" 를 압니다. 월드 · 설정은 AI 보다 오래 살아야 합니다.
     */
    class SW_GF_API RtsAiController
    {
    public:
        RtsAiController();

        void initialize( RtsWorld* pWorld, int32 player, const RtsAiSettings& settings );
        /** @brief `_thinkInterval` 마다 트리를 한 번 돌립니다. */
        void update( float32 deltaTime );
        /** @brief 월드 알림을 받습니다(공격받음). */
        void notify( const RtsEvent& event );

        const utf8*       getActiveTaskName() const { return _runner.getActiveLeafName(); }
        int32             getPlayer() const { return _player; }
        int32             getAttackWaveCount() const { return _attackWaveCount; }
        const Blackboard& getBlackboard() const { return _blackboard; }

    private:
        static BehaviorStatus taskDefend( BehaviorContext& context );
        static BehaviorStatus taskGatherIdle( BehaviorContext& context );
        static BehaviorStatus taskTrainWorkers( BehaviorContext& context );
        static BehaviorStatus taskBuildSupply( BehaviorContext& context );
        static BehaviorStatus taskBuildProduction( BehaviorContext& context );
        static BehaviorStatus taskTrainArmy( BehaviorContext& context );
        static BehaviorStatus taskAttack( BehaviorContext& context );

        void makeTree();
        /** @brief 노는 · 채취 중인 일꾼 하나로 @p buildingId 를 본진 둘레에 짓습니다. */
        bool      orderConstruction( const hashed_string& buildingId );
        RtsUnitId findDepot() const;
        void      collectArmy( vector<RtsUnitId>& outListUnit ) const;

        BehaviorTree       _tree;
        BehaviorTreeRunner _runner;
        Blackboard         _blackboard;
        RtsAiSettings      _settings;
        RtsWorld*          _pWorld;
        float32            _thinkTimer;
        int32              _player;
        int32              _attackWaveCount;
    };
} // namespace sw
