#include "pch.h"

#include "Games/Shooter3D/ShooterAutoAimControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterAutoAimControllerComponentInternal
        {
            static constexpr const utf8* kFire         = "Fire";
            static constexpr const utf8* kArrWeapon[2] = { "Weapon1", "Weapon2" }; ///< 소총 · 산탄총

            /**
             * @brief 눈에서 점까지 막는 상자가 없으면 true 입니다(시야). 상자는 디렉터가 모은 막는 상자 — 그 상자로 정적 강체를 만들므로 물리 질의와 같은 벽이다.
             * @details 생각은 틱(워커)에서 돌아 물리 씬을 묻지 않는다(물리 씬은 게임 스레드만). 같은 상자 목록을 기하로 본다.
             */
            static bool isVisible( const float3& eye, const float3& point, const vector<ShooterArenaBox>& listBox )
            {
                const float3  toPoint  = point - eye;
                const float32 distance = toPoint.getLength();
                if ( distance <= 1.0e-4f )
                    return true;
                GameRay ray;
                ray._origin    = eye;
                ray._direction = toPoint * ( 1.0f / distance );
                for ( const ShooterArenaBox& box : listBox )
                {
                    float32 hitDistance = 0.0f;
                    if ( RayMath::intersectAABB( ray, box._min, box._max, distance, hitDistance ) && hitDistance < distance )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterAutoAimControllerComponent::ShooterAutoAimControllerComponent()
        : _engageDistance{ 7.0f }
        , _closeRangeDistance{ 8.0f }
        , _aimTolerance{ 3.0f * MathUtil::kDegreeToRadian }
        , _chestHeight{ 0.55f }
        , _circleTangent{ 0.08f }
        , _circleInward{ 0.04f }
    {
        // 사람처럼 돈다 — 표적이 바뀔 때 한 프레임에 수십 도 돌면 3인칭 몸 · 카메라가 튄다.
        setTurnRate( 3.0f );
    }

    void ShooterAutoAimControllerComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal = ShooterAutoAimControllerComponentInternal;
        (void)context;
        const GameObject*               pPawnOwner = pawn.getOwner();
        const GameObjectManager*        pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const ShooterPlayerComponent*   pPlayer    = pPawnOwner != nullptr ? pPawnOwner->getComponent<ShooterPlayerComponent>() : nullptr;
        const ShooterDirectorComponent* pDirector =
            pManager != nullptr && pPlayer != nullptr ? GameDirectorComponent::resolve<ShooterDirectorComponent>( *pManager, pPlayer->getDirector() ) : nullptr;
        if ( pDirector == nullptr || pPlayer->isAlive() == false )
        {
            stopMoving();
            clearFocus();
            return;
        }

        // 아레나 가운데를 도는 나선 — 접선으로 돌고 안쪽으로 조금 끈다. 가운데 가까이에서는 그 점이 도착 거리 안이라 선다.
        const float3 feet = pPlayer->getFeetPosition();
        const float3 circle =
            float3{ -feet._z * _circleTangent - feet._x * _circleInward, 0.0f, feet._x * _circleTangent - feet._z * _circleInward };
        moveTo( float3{ feet._x + circle._x, 0.0f, feet._z + circle._z } );

        // 보이는 적 가운데 가장 가까운 가슴. 보이는 적이 없으면 가장 가까운 적을 겨누되 쏘지 않는다(엄폐물에 탄을 버리지 않는다).
        const float3                    eye          = pPlayer->getEyePosition();
        float32                         bestDistance = MathUtil::kMaxFloat;
        float3                          aimPoint{ 0.0f, 0.0f, 0.0f };
        bool                            bVisible = false;
        const vector<ShooterEnemyView>& listView = pDirector->getEnemyViews();
        const ShooterEnemyView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const float3  chest          = pView[viewIndex]._position + float3{ 0.0f, pView[viewIndex]._height * _chestHeight, 0.0f };
            const float32 distance       = float3::getDistance( eye, chest );
            const bool    bChestVisible  = Internal::isVisible( eye, chest, pDirector->getBoxes() );
            const bool    bBetterVisible = bChestVisible && ( bVisible == false || distance < bestDistance );
            const bool    bBetterHidden  = bVisible == false && bChestVisible == false && distance < bestDistance;
            if ( bBetterVisible || bBetterHidden )
            {
                bestDistance = distance;
                aimPoint     = chest;
                bVisible     = bChestVisible;
            }
        }
        // 상태(탐침 `AI.State`): 적이 없으면 Patrol, 겨누는 중이면 Aim, 이번 틱에 쏘면 Fire.
        if ( listView.empty() )
        {
            setStateName( "Patrol" );
            clearFocus();
            return;
        }
        setStateName( "Aim" );
        setFocus( aimPoint );
        const float3  toTarget  = aimPoint - eye;
        const float32 targetYaw = MathUtil::atan2( toTarget._x, toTarget._z );
        const float32 yawError  = MathUtil::wrapAngle( targetYaw - getControlYaw() );
        const bool    bOnTarget = bVisible && MathUtil::abs( yawError ) < _aimTolerance && bestDistance < _engageDistance;
        getBlackboard().setFloat( "TargetDistance", bestDistance );
        if ( bOnTarget )
        {
            setStateName( "Fire" );
            pressButton( hashed_string( Internal::kFire ) );
        }
        // 가까우면 산탄총, 멀면 소총 — 탄이 남은 쪽만.
        const int32        wantedWeapon = bestDistance < _closeRangeDistance ? 1 : 0;
        const WeaponState& wanted       = pPlayer->getWeapon( wantedWeapon );
        if ( wantedWeapon != pPlayer->getWeaponIndex() && wanted.getMagazineAmmo() + wanted.getReserveAmmo() > 0 )
            pressButton( hashed_string( Internal::kArrWeapon[wantedWeapon] ) );
    }
} // namespace sw
