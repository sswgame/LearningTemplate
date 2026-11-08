#include "pch.h"

#include "Games/Shooter3D/ShooterAutoAimControllerComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/Math/Ray.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterAutoAimControllerComponentInternal
        {
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
                Ray ray;
                ray._origin    = eye;
                ray._direction = toPoint * ( 1.0f / distance );
                for ( const ShooterArenaBox& box : listBox )
                {
                    float32 hitDistance = 0.0f;
                    if ( Ray::intersectAABB( ray, box._min, box._max, distance, hitDistance ) && hitDistance < distance )
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
        , _bMelee{ SW_FALSE }
    {
        // 사람처럼 돈다 — 표적이 바뀔 때 한 프레임에 수십 도 돌면 3인칭 몸 · 카메라가 튄다.
        setTurnRate( 3.0f );
        requireBehaviorTree();
    }

    void ShooterAutoAimControllerComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal = ShooterAutoAimControllerComponentInternal;
        (void)context;
        Blackboard&                     blackboard = getBlackboard();
        const GameObject*               pPawnOwner = pawn.getOwner();
        const GameObjectManager*        pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const ShooterPlayerComponent*   pPlayer    = pPawnOwner != nullptr ? pPawnOwner->getComponent<ShooterPlayerComponent>() : nullptr;
        const ShooterGameModeComponent* pGameMode =
            pManager != nullptr && pPlayer != nullptr ? GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pPlayer->getGameMode() ) : nullptr;
        if ( pGameMode == nullptr || pPlayer->isAlive() == false )
        {
            blackboard.clearValue( "CirclePoint" ); // 트리가 서고 초점을 지운다
            return;
        }

        // 아레나 가운데를 도는 나선 — 접선으로 돌고 안쪽으로 조금 끈다. 가운데 가까이에서는 그 점이 도착 거리 안이라 선다.
        const float3 feet   = pPlayer->getFeetPosition();
        const float3 circle = float3{ -feet._z * _circleTangent - feet._x * _circleInward, 0.0f, feet._x * _circleTangent - feet._z * _circleInward };
        blackboard.setVector( "CirclePoint", float3{ feet._x + circle._x, 0.0f, feet._z + circle._z } );

        // 보이는 적 가운데 가장 가까운 가슴. 보이는 적이 없으면 가장 가까운 적을 겨누되 쏘지 않는다(엄폐물에 탄을 버리지 않는다 — 트리가 TargetVisible 을 본다).
        const float3                    eye          = pPlayer->getEyePosition();
        float32                         bestDistance = MathUtil::kMaxFloat;
        float3                          aimPoint{ 0.0f, 0.0f, 0.0f };
        bool                            bVisible = false;
        const vector<ShooterEnemyView>& listView = pGameMode->getEnemyViews();
        const ShooterEnemyView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const float3  chest          = pView[viewIndex]._position + float3{ 0.0f, pView[viewIndex]._height * _chestHeight, 0.0f };
            const float32 distance       = float3::getDistance( eye, chest );
            const bool    bChestVisible  = Internal::isVisible( eye, chest, pGameMode->getBoxes() );
            const bool    bBetterVisible = bChestVisible && ( bVisible == false || distance < bestDistance );
            const bool    bBetterHidden  = bVisible == false && bChestVisible == false && distance < bestDistance;
            if ( bBetterVisible || bBetterHidden )
            {
                bestDistance = distance;
                aimPoint     = chest;
                bVisible     = bChestVisible;
            }
        }
        blackboard.setBool( "HasEnemy", listView.empty() == false );
        if ( listView.empty() )
            return;
        const float3  toTarget  = aimPoint - eye;
        const float32 targetYaw = MathUtil::atan2( toTarget._x, toTarget._z );
        blackboard.setVector( "AimPoint", aimPoint );
        blackboard.setBool( "TargetVisible", bVisible );
        blackboard.setFloat( "TargetDistance", bestDistance );
        blackboard.setFloat( "AimError", MathUtil::abs( MathUtil::wrapAngle( targetYaw - getControlYaw() ) ) );
        blackboard.setFloat( "AimTolerance", _aimTolerance );
        blackboard.setFloat( "EngageDistance", _engageDistance );
        blackboard.setFloat( "CloseRangeDistance", _closeRangeDistance );
        blackboard.setFloat( "MeleeReach", pPlayer->getMeleeReach() );
        const ShooterAutoPlayOptions* const pOptions = game::findSubsystem<ShooterAutoPlayOptions>();
        blackboard.setBool( "Melee", _bMelee == SW_TRUE || ( pOptions != nullptr && pOptions->isMeleeForced() ) );
        // 무기: 0 소총 · 1 산탄총. 탄은 탄창 + 예비 — 트리가 남은 쪽만 고른다.
        const WeaponState& rifle   = pPlayer->getWeapon( 0 );
        const WeaponState& shotgun = pPlayer->getWeapon( 1 );
        blackboard.setInt( "WeaponIndex", pPlayer->getWeaponIndex() );
        blackboard.setInt( "RifleAmmo", rifle.getMagazineAmmo() + rifle.getReserveAmmo() );
        blackboard.setInt( "ShotgunAmmo", shotgun.getMagazineAmmo() + shotgun.getReserveAmmo() );
    }
} // namespace sw
