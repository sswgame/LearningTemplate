/**
 * @file AiControllerComponent.h
 * @brief AI 조종자 — 판단(`think`) → 의도. 플레이어와 같은 폰 이동으로 움직입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Navigation/INavMover.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/ControllerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class AiControllerComponent
     * @brief AI 조종자 — 파생이 `think` 에서 목표를 정하면(이동 목적지 · 바라볼 곳 · 누를 버튼) 이 클래스가 의도로 옮깁니다. 행동 트리 · 감독은 `think` 안에서 돈다.
     * @details 언리얼 `AAIController` 의 자리입니다. 움직이는 것은 플레이어와 같은 폰 이동 컴포넌트입니다 — 이 조종자는 이동 축만 냅니다.
     *          폰 오브젝트에 내비 에이전트(`NavMeshAgentComponent`, 방식 `SteerOnly`)가 있으면 경로 · 군중 회피로 정한 속도를 이동 축으로 넣고,
     *          없으면 목적지로 곧장(장애물 무시) 갑니다. 바라보기는 초점이 있으면 그쪽, 없으면 움직이는 쪽으로 `_turnRate` 만큼 조종 요를 돌립니다.
     */
    REFLECT( Category = "Control", DisplayName = "AI Controller", Tooltip = "AI: decides goals in think(); moves through the pawn's own movement like a player" )
    class SW_GF_API AiControllerComponent : public ControllerComponent
    {
    public:
        REFLECT_BODY();

        AiControllerComponent();
        ~AiControllerComponent() override = default;

        void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) final;

        /** @brief 목적지로 갑니다(같은 곳이면 다시 걸지 않는다). `think` 안 · 밖 어디서든(게임 스레드). */
        void moveTo( const float3& destination );
        /** @brief 멈춥니다(목적지를 지운다). */
        void stopMoving();
        /** @brief 바라볼 곳입니다 — 조종 요 · 피치를 `_turnRate` 로 그쪽으로 돌립니다. */
        void setFocus( const float3& worldPoint );
        /** @brief 초점을 지웁니다 — 움직이는 쪽을 봅니다. */
        void clearFocus();
        /** @brief 이번 틱에 버튼을 한 번 누릅니다(발동 + 누름). 폰 스키마에 없는 이름이면 할 일이 없습니다. */
        void pressButton( const hashed_string& name );
        /** @brief 버튼을 누르고 있거나(@p bHeld) 뗍니다 — 다시 부를 때까지 유지합니다. */
        void holdButton( const hashed_string& name, bool bHeld );
        /** @brief 이번 틱의 아날로그 값입니다. */
        void setAnalog( const hashed_string& name, float32 value );
        /** @brief 이동 상태입니다 — 내비 에이전트가 있으면 그 상태, 없으면 목적지가 있으면 Moving, 닿았으면 Arrived. */
        NavMoveStatus getMoveStatus() const;
        bool          hasDestination() const { return _bHasDestination == SW_TRUE; }
        const float3& getDestination() const { return _destination; }
        float32       getTurnRate() const { return _turnRate; }
        void          setTurnRate( float32 turnRate ) { _turnRate = turnRate; }
        float32       getArriveDistance() const { return _arriveDistance; }
        void          setArriveDistance( float32 distance ) { _arriveDistance = distance; }

    protected:
        /** @brief 판단 — 파생이 덮습니다. 기본은 아무것도 하지 않습니다(이미 건 목적지 · 초점은 그대로 따른다). */
        virtual void think( const ControlFrameContext& context, const PawnComponent& pawn )
        {
            (void)context;
            (void)pawn;
        }

    private:
        /** @brief 이번 틱에 갈 월드 방향(XZ, 길이 0..1)을 정합니다. 닿았으면 목적지를 지웁니다. */
        float3 computeMoveDirection( const PawnComponent& pawn, const float3& pawnPosition );

    private:
        PROPERTY( Category = "AI", DisplayName = "Turn Rate", Min = 0.0, Tooltip = "How fast the control rotation turns to the focus or the move direction", Units = "rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "AI", DisplayName = "Arrive Distance", Min = 0.0, Tooltip = "Stops inside this distance of the destination when there is no navmesh agent", Units = m )
        float32 _arriveDistance;

        ControlIntent          _pending; ///< 이번 틱에 쌓인 버튼 발동 · 아날로그
        float3                 _destination;
        float3                 _focusPoint;
        uint32                 _heldButtonMask; ///< `holdButton` 으로 누르고 있는 버튼
        NavMoveStatus          _moveStatus;     ///< 내비 에이전트가 없을 때의 상태
        uint8                  _bHasDestination  : 1;
        uint8                  _bHasFocus        : 1;
        uint8                  _bDestinationSent : 1; ///< 지금 목적지를 내비 에이전트에 걸었다
        uint8                  _bStopPending     : 1; ///< 다음 틱에 내비 에이전트를 멈춘다
        [[maybe_unused]] uint8 _reserved         : 4;
    };
} // namespace sw
