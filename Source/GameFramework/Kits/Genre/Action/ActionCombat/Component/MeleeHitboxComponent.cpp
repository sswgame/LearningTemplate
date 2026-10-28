#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/MeleeHitboxComponent.h"

#include "Core/Foundation/StdHeaders.h"

#include "Engine/Character/Hit/MeleeHitWindow.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "MeleeHitbox" );

    namespace
    {
        /** @brief 칼날 표본점 수입니다. 두 끝과 가운데 — 칼날이 돌며 지나간 자리를 끝점 둘만으로는 놓친다. */
        constexpr uint32 kMeleeHitboxBladeSampleCount = 3;
    } // namespace
} // namespace sw

namespace sw
{
    MeleeHitboxComponent::MeleeHitboxComponent()
        : _listHitTarget{}
        , _listPreviousPoint{}
        , _bladeStart{}
        , _bladeEnd{}
        , _bladeRadius{ 0.0f }
        , _damage{ 0 }
        , _duration{ 0.0f }
        , _currentDuration{ 0.0f }
        , _bIsAttacking{ false }
    {
    }

    void MeleeHitboxComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 물리가 이번 프레임 위치로 옮긴 뒤에 쓴다.
        setTickGroup( TickGroup::PostPhysics );
        _listPreviousPoint.clear();
    }

    void MeleeHitboxComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );

        if ( _bIsAttacking == false )
            return;

        requestSweep();
        _currentDuration += deltaTime;
        if ( _duration > 0.0f && _currentDuration >= _duration )
        {
            _bIsAttacking    = false;
            _currentDuration = 0.0f;
        }
    }

    void MeleeHitboxComponent::setBlade( const float3& bladeStart, const float3& bladeEnd, float32 radius )
    {
        _bladeStart  = bladeStart;
        _bladeEnd    = bladeEnd;
        _bladeRadius = radius;
    }

    void MeleeHitboxComponent::beginAttack( int32 damage, float32 duration )
    {
        if ( ( _bladeEnd - _bladeStart ).getLengthSquared() <= 1.0e-12f )
        {
            SW_LOG_ERROR( "MeleeHitboxComponent: blade has no length - call setBlade() with two different points before beginAttack()" );
            return;
        }
        _damage          = damage;
        _duration        = duration;
        _currentDuration = 0.0f;
        _bIsAttacking    = true;
        _listHitTarget.clear();
        _listPreviousPoint.clear();
        // 휘두르기 전부터 칼날 안에 있던 오브젝트도 맞는다.
        requestSweep();
    }

    GameObject* MeleeHitboxComponent::findAttackerRoot() const
    {
        GameObject* pRoot = getOwner();
        while ( pRoot != nullptr && pRoot->getParent() != nullptr )
        {
            pRoot = pRoot->getParent();
        }
        return pRoot;
    }

    void MeleeHitboxComponent::collectBlade( vector<float3>& outListPoint ) const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const float4x4        world  = pScene != nullptr ? pScene->getWorldMatrix() : float4x4::Identity;
        MeleeHitWindowUtil::makeBlade( float3::transform( _bladeStart, world ), float3::transform( _bladeEnd, world ), kMeleeHitboxBladeSampleCount, outListPoint );
    }

    void MeleeHitboxComponent::requestSweep()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( MeleeHitboxComponent& self, GameObjectManager& manager )
        {
            (void)manager;
            self.sweepBlade();
        } );
    }

    void MeleeHitboxComponent::sweepBlade()
    {
        GameObject* pAttacker = findAttackerRoot();
        if ( pAttacker == nullptr )
            return;
        vector<float3> listCurrent;
        collectBlade( listCurrent );
        MeleeHitParams params;
        params._radius = _bladeRadius;
        params._damage = static_cast<float32>( _damage );
        params._bIs2D  = true;
        vector<MeleeHit> listHit;
        (void)MeleeHitWindowUtil::sweep( *pAttacker, _listPreviousPoint, listCurrent, params, MeleeHitWindowUtil::getProcessAuthority(), _listHitTarget, listHit );
        _listPreviousPoint = std::move( listCurrent );
    }
} // namespace sw
