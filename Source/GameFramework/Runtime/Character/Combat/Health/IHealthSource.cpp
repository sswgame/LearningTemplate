#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Health/IHealthSource.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Combat/Health/IHealthListener.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"

namespace sw
{
    namespace
    {
        /** @brief 읽기의 비율 0..1 입니다. 최대가 0 이하이거나 체력이 없는 주인이면 0 입니다. */
        float32 computeRatio( const HealthReading& reading )
        {
            if ( reading._bHasHealth == SW_FALSE || reading._maxHealth <= 0.0f )
                return 0.0f;
            return MathUtil::saturate( reading._health / reading._maxHealth );
        }

        /**
         * @brief 주인의 쓰러짐 태그를 맞춥니다. 저작 중(플레이 밖)에는 손대지 않습니다 — 태그가 씬 파일에 박힌다.
         * @details 태그 쓰기는 틱 뒤다. 미룬 동안 다시 바뀔 수 있어 그때의 체력을 다시 읽는다(한 틱 안의 쓰러짐 → 부활).
         */
        void syncDeadTag( const GameObject& owner, bool bDead )
        {
            const GameObjectManager* const pManager = owner.getManager();
            if ( pManager == nullptr || pManager->hasBegunPlay() == false || owner.hasTag( TargetFilter::getDeadTag(), true ) == bDead )
                return;
            const GameObjectHandle handle = owner.getHandle();
            pManager->executeOrDeferPostTickOnScene( [handle]( GameObjectManager& manager )
            {
                GameObject* const          pObject = manager.resolveGameObject( handle );
                const IHealthSource* const pSource = pObject != nullptr ? pObject->findInterface<IHealthSource>() : nullptr;
                if ( pSource == nullptr )
                    return;
                if ( pSource->getHealthReading()._bDead == SW_TRUE )
                    pObject->addTag( TargetFilter::getDeadTag() );
                else
                    pObject->removeTag( TargetFilter::getDeadTag() );
            } );
        }
    } // namespace
} // namespace sw

namespace sw
{
    float32 IHealthSource::getHealthRatio() const
    {
        return computeRatio( getHealthReading() );
    }

    void IHealthSource::notifyHealthChanged( const IHealthSource& source, const GameObject* pOwner, bool bReset )
    {
        if ( pOwner == nullptr )
            return;
        const HealthReading reading = source.getHealthReading();
        if ( reading._bHasHealth == SW_FALSE )
            return;

        HealthChangedEvent event;
        event._ratio = computeRatio( reading );
        if ( bReset )
            event._kind = HealthChangeKind::Reset;
        else if ( reading._bDead == SW_TRUE )
            event._kind = HealthChangeKind::Died;
        else
            event._kind = HealthChangeKind::Changed;
        syncDeadTag( *pOwner, reading._bDead == SW_TRUE );
        IHealthListener::broadcast( *pOwner, event );
    }

    void IHealthSource::deliverFatalHit( GameObject* pOwner, const HitInfo& hit )
    {
        // 이미 치명 표시가 달린 맞음이면 다른 컴포넌트도 그 표시로 받았다 — 두 번 보내지 않는다.
        if ( pOwner == nullptr || hit._bFatal )
            return;
        HitInfo fatal = hit;
        fatal._bFatal = true;
        // 같은 맞음 사건으로 다시 낸다 — 체력 원천(이 컴포넌트 포함)은 치명 표시가 달린 맞음을 건너뛴다(피해는 이미 깎였다).
        GameObjectEvents::broadcast( *pOwner, &GameObjectEvents::_hitReceived, fatal );
    }
} // namespace sw
