#include "pch.h"

#include "Games/Shooter3D/ShooterEnemyAIControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterEnemyComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterEnemyAIControllerComponentInternal
        {
            static constexpr const utf8* kAttack = "Attack";

            /** @brief 몸의 단계를 판단 상태 이름으로 바꿉니다(탐침 `AI.State`). */
            static const utf8* getStateName( ShooterEnemyPhase phase )
            {
                switch ( phase )
                {
                    case ShooterEnemyPhase::Rising:
                        return "Rise";
                    case ShooterEnemyPhase::Chasing:
                        return "Chase";
                    case ShooterEnemyPhase::Attacking:
                        return "Attack";
                    case ShooterEnemyPhase::Staggered:
                        return "Stagger";
                    case ShooterEnemyPhase::Dying:
                        return "Die";
                }
                return "Chase";
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterEnemyAIControllerComponent::ShooterEnemyAIControllerComponent()
        : _retargetDistance{ 0.5f }
        , _focusHeight{ 1.0f }
    {
    }

    void ShooterEnemyAIControllerComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal = ShooterEnemyAIControllerComponentInternal;
        (void)context;
        const GameObject*               pPawnOwner = pawn.getOwner();
        const GameObjectManager*        pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const ShooterEnemyComponent*    pEnemy     = pPawnOwner != nullptr ? pPawnOwner->getComponent<ShooterEnemyComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector =
            pManager != nullptr && pEnemy != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, pEnemy->getDirector() ) : nullptr;
        const hashed_string attack( Internal::kAttack );
        if ( pDirector == nullptr || pEnemy->isAlive() == false )
        {
            holdButton( attack, false );
            stopMoving();
            clearFocus();
            return;
        }
        const float3& target   = pDirector->getPlayerFeet();
        const float3& position = pEnemy->getPosition();
        const float32 deltaX   = target._x - position._x;
        const float32 deltaZ   = target._z - position._z;
        const float32 distance = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        const float3  focus    = target + float3{ 0.0f, _focusHeight, 0.0f };
        holdButton( attack, distance < pEnemy->getReach() && pDirector->isPlayerAlive() );

        const ShooterEnemyPhase phase = pEnemy->getPhase();
        setStateName( hashed_string( Internal::getStateName( phase ) ) );
        if ( phase != ShooterEnemyPhase::Chasing )
        {
            // 일어남 · 휘두름 · 움찔 · 쓰러짐 — 몸이 멈춰 있다. 휘두르는 동안은 플레이어 쪽으로 돈다.
            stopMoving();
            if ( phase == ShooterEnemyPhase::Attacking )
                setFocus( focus );
            else
                clearFocus();
            return;
        }
        if ( distance <= pEnemy->getReach() * pEnemy->getStopFraction() )
        {
            stopMoving();
            setFocus( focus );
            return;
        }
        // 쫓는다 — 움직이는 쪽을 본다. 플레이어가 조금 움직일 때마다 경로를 다시 잡지 않는다.
        clearFocus();
        const float3& destination = getDestination();
        const float32 movedX      = target._x - destination._x;
        const float32 movedZ      = target._z - destination._z;
        if ( hasDestination() == false || movedX * movedX + movedZ * movedZ > _retargetDistance * _retargetDistance )
            moveTo( target );
    }
} // namespace sw
