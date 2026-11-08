#include "pch.h"

#include "Engine/Animation/Scene/AnimationFrameObserver.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록된 관찰자(이름 순). Engine 이미지의 함수 정적이라 등록한 이미지보다 오래 산다. */
        SW_DEFINE_REGISTRATION_LIST( getAnimationFrameObserverRegistrations, const AnimationFrameObserverRegistration, "AnimationFrameObserverRegistry", RegistrationOrder::ByOrderThenName, true )
    } // namespace

    bool AnimationFrameObserverRegistry::registerObserver( const AnimationFrameObserverRegistration* pRegistration )
    {
        const bool bInvalidRegistration = pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pCreate == nullptr;
        if ( bInvalidRegistration )
            return false;
        return getAnimationFrameObserverRegistrations().addLogged( pRegistration, "animation frame observer", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void AnimationFrameObserverRegistry::unregisterObserver( const AnimationFrameObserverRegistration* pRegistration )
    {
        (void)getAnimationFrameObserverRegistrations().remove( pRegistration ); // 등록되어 있지 않은 관찰자를 빼는 것은 할 일이 없다
    }

    void AnimationFrameObserverRegistry::createAll( vector<unique_ptr<IAnimationFrameObserver>>& outListObserver )
    {
        const RegistrationList<const AnimationFrameObserverRegistration>& registrations = getAnimationFrameObserverRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            outListObserver.push_back( registrations.getAt( index )->_pCreate() );
        }
    }
} // namespace sw
