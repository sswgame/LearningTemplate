/**
 * @file AIControllerComponent.h
 * @brief AI 조종자입니다. 감각(`updateBlackboard`)이 블랙보드를 채우고 행동 트리가 판단해 의도를 만들고, 플레이어와 같은 폰 이동으로 움직입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Navigation/INavMover.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"

namespace sw
{
    class AIControllerComponent;
    class BehaviorTreeAsset;
    class NavMeshAgentComponent;

    /** @brief 행동 트리의 동작 · 조건(`ai.*` · `aiHasDestination`)이 쓰는 조종자 칸입니다. 조종자가 자기 트리를 돌릴 때만 채운다 — 다른 실행기(지휘관 AI · 스케줄)에서는 없어 동작이 실패한다. */
    struct AIControllerSlot
    {
        using Type                      = AIControllerComponent;
        static constexpr uint32 kSlotID = FourCcUtil::make( "AICT" );
    };

    /**
     * @class AIControllerComponent
     * @brief AI 조종자입니다. 판단은 행동 트리 데이터(`_behaviorTree`, `*.bt.xml`)가 정하고, 파생은 감각(`updateBlackboard` — 블랙보드에 대상 · 거리 · 상태를 적는다)만 냅니다.
     *        트리의 잎은 공통 동작 어휘(`ai.moveTo` · `ai.stop` · `ai.focus` · `ai.press` · `ai.hold` · `ai.state` …, `AIControllerActions.h`)로 목표(이동 목적지, 바라볼 곳, 누를 버튼)를 정하고, 이 클래스가 의도로 옮깁니다.
     * @details 언리얼 `AAIController` + `RunBehaviorTree` 에 해당합니다(감각은 BT 서비스 · AIPerception 자리). 새 적 · NPC 는 트리 데이터 한 장과 감각 하나로 더한다 — 판단 `switch` 를 코드에 두지 않는다.
     *          판단을 코드로 짜는 길은 없다 — 판단(`think`)은 덮어쓸 수 없고 파생은 감각만 낸다(자동 조준 · 자동 플레이도 감각 + 트리다). 트리가 없는 조종자는 밖에서 건 목적지 · 초점만 따른다.
     *          움직이는 것은 플레이어와 같은 폰 이동 컴포넌트이고, 이 조종자는 이동 축만 냅니다.
     *          폰 오브젝트에 내비 에이전트(`NavMeshAgentComponent`, 방식 `SteerOnly`)가 있으면 경로 · 군중 회피로 정한 속도를 이동 축으로 넣고,
     *          없으면 목적지로 곧장(장애물 무시) 갑니다. 바라보기는 초점이 있으면 그쪽, 없으면 움직이는 쪽으로 `_turnRate` 만큼 조종 요를 돌립니다.
     */
    REFLECT( Category = "Control", DisplayName = "AI Controller", Tooltip = "AI: its behavior tree decides goals from the blackboard the controller senses; moves through the pawn's own movement like a player" )
    class SW_GF_API AIControllerComponent : public ControllerComponent
    {
    public:
        REFLECT_BODY();

        AIControllerComponent();
        ~AIControllerComponent() override = default;

        void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) final;

        /** @brief 목적지로 갑니다(같은 곳이면 다시 걸지 않는다). `think` 안 · 밖 어디서든(게임 스레드). */
        void moveTo( const float3& destination );
        /** @brief 멈춥니다(목적지를 지운다). */
        void stopMoving();
        /** @brief 바라볼 곳을 정합니다. 조종 요와 피치를 `_turnRate` 속도로 그쪽으로 돌립니다. */
        void setFocus( const float3& worldPoint );
        /** @brief 초점을 지웁니다. 그 뒤로는 움직이는 쪽을 봅니다. */
        void clearFocus();
        /** @brief 다음에 내는 의도에서 버튼을 한 번 누릅니다(발동과 누름). `think` 안팎 어디서든 부를 수 있습니다. 폰 스키마에 없는 이름이면 아무 일도 하지 않습니다. */
        void pressButton( const hashed_string& name );
        /** @brief 버튼을 누르고 있거나(@p bHeld) 뗍니다. 다시 부를 때까지 유지합니다. */
        void holdButton( const hashed_string& name, bool bHeld );
        /** @brief 다음에 내는 의도의 아날로그 값을 정합니다. 그 한 틱에만 쓰이며 `think` 안팎 어디서든 부를 수 있습니다. */
        void setAnalog( const hashed_string& name, float32 value );
        /** @brief 이동 상태입니다. 내비 에이전트가 있으면 그 상태이고, 없으면 목적지가 있을 때 Moving, 닿았을 때 Arrived 입니다. */
        NavMoveStatus getMoveStatus() const;
        bool          hasDestination() const { return _bHasDestination == SW_TRUE; }
        const float3& getDestination() const { return _destination; }
        float32       getTurnRate() const { return _turnRate; }
        void          setTurnRate( float32 turnRate ) { _turnRate = turnRate; }
        float32       getArriveDistance() const { return _arriveDistance; }
        void          setArriveDistance( float32 distance ) { _arriveDistance = distance; }

        /**
         * @brief 판단의 지금 상태 이름입니다(상태 머신의 상태 · 고른 할 일). 파생이 `think` 에서 상태가 바뀔 때 부릅니다.
         * @details 디버그 표시와 자동화 감시 값 `AI.State` 가 읽는다(`<ExpectVisited probe="AI.State" sequence="Till,Plant"/>`). 언리얼 Gameplay Debugger 의 BT 줄과 상태 줄에 해당한다.
         */
        void                 setStateName( const hashed_string& stateName ) { _stateName = stateName; }
        const hashed_string& getStateName() const { return _stateName; }
        /** @brief 이 조종자의 블랙보드입니다(언리얼 `AAIController` 의 Blackboard 컴포넌트). 행동 트리 · 판단이 쓰고 감시 값 `AI.Blackboard` 가 읽습니다. */
        Blackboard&       getBlackboard() { return _blackboard; }
        const Blackboard& getBlackboard() const { return _blackboard; }
        /** @brief 판단이 도는 행동 트리 실행기입니다. 트리가 없으면 nullptr 입니다. 감시 값 `AI.BehaviorTask` · 행동 트리 창 · 디버그 HUD 가 읽습니다. */
        const BehaviorTreeRunner* getBehaviorTreeRunner() const { return _pBehaviorTree != nullptr ? &_runner : nullptr; }

    protected:
        /** @brief 감각입니다. 트리가 읽을 값을 블랙보드에 적습니다(없는 값은 지운다 — 트리가 `hasBlackboard` 로 묻는다). 기본은 아무것도 하지 않습니다. */
        virtual void updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
        {
            (void)context;
            (void)pawn;
        }
        /** @brief 이 조종자는 트리 없이 판단하지 못합니다(파생 생성자가 부른다). 트리 경로가 비었거나 읽지 못하면 오류를 남기고 서 있습니다. */
        void requireBehaviorTree() { _bTreeRequired = SW_TRUE; }

    private:
        /**
         * @brief 판단입니다. 감각(`updateBlackboard`)을 부른 뒤 행동 트리를 한 틱 돌립니다. 트리가 없으면 이미 정한 목적지와 초점을 그대로 따릅니다.
         * @details 가상이 아니다 — 판단은 트리 데이터의 일이고 파생은 감각만 덮어쓴다(언리얼 `AAIController` + BT 서비스 · 트리의 나눔).
         */
        void think( const ControlFrameContext& context, const PawnComponent& pawn );
        /** @brief 이번 틱에 갈 월드 방향(XZ, 길이 0..1)을 정합니다. 닿았으면 목적지를 지웁니다. */
        float3 computeMoveDirection( const PawnComponent& pawn, const float3& pawnPosition );
        /** @brief 내비 에이전트(SteerOnly)가 낸 속도를 이동 방향으로 씁니다. 목적지를 한 번 요청하고, 닿거나 경로가 없으면 목적지를 지웁니다. */
        float3 computeAgentMoveDirection( NavMeshAgentComponent& agent );
        /** @brief 트리를 처음 한 번 읽어 실행기를 준비합니다. 읽지 못하면 오류를 한 번 남기고 false 입니다(다시 읽지 않는다). 트리가 없고 필요도 없으면 false 입니다. */
        bool prepareBehaviorTree();

    private:
        PROPERTY( Category = "AI", DisplayName = "Turn Rate", Min = 0.0, Tooltip = "How fast the control rotation turns to the focus or the move direction", Units = "rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "AI", DisplayName = "Arrive Distance", Min = 0.0, Tooltip = "Stops inside this distance of the destination when there is no navmesh agent", Units = m )
        float32 _arriveDistance;
        PROPERTY( Category = "AI", DisplayName = "Behavior Tree", Tooltip = "Behavior tree (.bt.xml) that decides every think; its leaves are the ai.* actions and its conditions read the blackboard the controller fills" )
        AssetRef<BehaviorTreeAsset> _behaviorTree;

        shared_ptr<const BehaviorTreeAsset> _pBehaviorTree; ///< 읽은 트리(공유 캐시의 한 벌). 실행기가 그 트리를 가리킨다
        BehaviorTreeRunner                  _runner;
        Blackboard                          _blackboard;
        hashed_string                       _stateName; ///< 판단의 지금 상태(`setStateName`)
        ControlIntent                       _pending;   ///< 이번 틱에 쌓인 버튼 발동 · 아날로그
        float3                              _destination;
        float3                              _focusPoint;
        uint32                              _heldButtonMask; ///< `holdButton` 으로 누르고 있는 버튼
        NavMoveStatus                       _moveStatus;     ///< 내비 에이전트가 없을 때의 상태
        uint8                               _bHasDestination  : 1;
        uint8                               _bHasFocus        : 1;
        uint8                               _bDestinationSent : 1; ///< 지금 목적지를 내비 에이전트에 걸었다
        uint8                               _bStopPending     : 1; ///< 다음 틱에 내비 에이전트를 멈춘다
        uint8                               _bWarnedDriveMode : 1; ///< 내비 에이전트가 SteerOnly 가 아니라고 경고했다
        uint8                               _bTreeRequired    : 1; ///< 트리 없이 판단하지 못한다(`requireBehaviorTree`)
        uint8                               _bTreePrepared    : 1; ///< 트리 읽기를 한 번 해 봤다(성공이든 실패든)
        [[maybe_unused]] uint8              _reserved         : 1;
    };
} // namespace sw
