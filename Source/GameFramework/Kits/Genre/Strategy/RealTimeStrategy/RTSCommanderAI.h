/**
 * @file RTSCommanderAI.h
 * @brief 컴퓨터 상대를 구현합니다. 행동 트리로 일꾼, 보급, 생산 건물, 병력을 늘리고, 공격받으면 막고, 병력이 차면 쳐들어갑니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "GameFramework/Base/Actor/AI/BehaviorTree.h"
#include "GameFramework/Base/Actor/AI/Blackboard.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

namespace sw
{
    /** @brief AI 가 쓰는 정의 id 와 목표 수입니다. 종족과 난이도에 따라 데이터로 바꿉니다. */
    struct RTSCommanderAISettings
    {
        hashed_string _workerID{};
        hashed_string _depotID{};      ///< 일꾼을 만드는 본진
        hashed_string _supplyID{};     ///< 보급 건물
        hashed_string _productionID{}; ///< 병력 건물
        hashed_string _armyUnitID{};   ///< 병력 유닛
        float32       _thinkInterval{ 0.5f };
        float32       _defendRadius{ 14.0f }; ///< 본진에서 이 거리 안의 공격만 막으러 간다
        int32         _workerTarget{ 14 };
        int32         _productionTarget{ 2 };
        int32         _attackArmySize{ 8 };
        int32         _supplyMargin{ 3 }; ///< 남은 보급이 이 값 이하이면 보급 건물을 짓는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RTSCommanderAI
     * @brief 플레이어 한 명을 맡는 AI 입니다. 트리는 데이터(`*.bt.xml`, 게임 팩의 `commander.bt.xml`)이고 아래 모양이 기본 데이터다. 반응형 셀렉터라서 위쪽 노드를 먼저 평가합니다.
     *        잎이 부르는 작업은 이 클래스가 `rts.defend` 같은 이름으로 `BehaviorTaskRegistry` 에 올린다(`SW_BEHAVIOR_TASK`) - 데이터가 순서 · 중단 규칙 · 가지를 정하고 코드는 작업만 가진다.
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
    class SW_KIT_API RTSCommanderAI
    {
    public:
        RTSCommanderAI();

        /**
         * @brief 월드 · 플레이어 · 설정으로 AI 를 시작합니다. 행동 트리는 데이터에서 읽은 것(`BehaviorTreeAsset::getTree`)을 복사해 쥔다 - 트리가 올바르지 않으면 어설트합니다(데이터 읽기가 이미 걸렀어야 한다).
         * @param tree 지휘관 트리입니다. 잎의 `task` 는 이 클래스가 등록한 `rts.*` 이름이어야 한다.
         */
        void initialize( RTSWorld* pWorld, int32 player, const RTSCommanderAISettings& settings, const BehaviorTree& tree );
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

        /** @brief 생각 간격 타이머와 공격 횟수를 씁니다. 행동 트리와 블랙보드는 쓰지 않으며, 읽은 쪽에서 트리를 처음부터 다시 고릅니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        // 트리 데이터(`*.bt.xml`)가 이름으로 가리키는 작업입니다(`RTSCommanderAI.cpp` 끝의 `SW_BEHAVIOR_TASK` 줄이 `rts.*` 이름을 붙인다).
        static BehaviorStatus taskDefend( BehaviorContext& context );
        static BehaviorStatus taskGatherIdle( BehaviorContext& context );
        static BehaviorStatus taskTrainWorkers( BehaviorContext& context );
        static BehaviorStatus taskBuildSupply( BehaviorContext& context );
        static BehaviorStatus taskBuildProduction( BehaviorContext& context );
        static BehaviorStatus taskTrainArmy( BehaviorContext& context );
        static BehaviorStatus taskAttack( BehaviorContext& context );

    private:
        /** @brief 놀거나 채취 중인 일꾼 하나로 @p buildingID 를 본진 둘레에 짓습니다. */
        bool      orderConstruction( const hashed_string& buildingID );
        RTSUnitID findDepot() const;
        void      collectArmy( vector<RTSUnitID>& outListUnit ) const;

        BehaviorTree           _tree;
        BehaviorTreeRunner     _runner;
        Blackboard             _blackboard;
        RTSCommanderAISettings _settings;
        RTSWorld*              _pWorld;
        Countdown              _thinkTimer;
        int32                  _player;
        int32                  _attackWaveCount;
    };
} // namespace sw
