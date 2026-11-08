#include "pch.h"

#include "Engine/Audio/AudioRenderListener.h"

#include "Core/String/RegistrationList.h"

namespace sw
{
    namespace
    {
        /** @brief 등록된 리스너(이름 순). Engine 이미지의 함수 정적이라 등록한 이미지보다 오래 산다. */
        SW_DEFINE_REGISTRATION_LIST( getAudioRenderListenerRegistrations, const AudioRenderListenerRegistration, "AudioRenderListenerRegistry", RegistrationOrder::ByOrderThenName, true )
    } // namespace

    bool AudioRenderListenerRegistry::registerListener( const AudioRenderListenerRegistration* pRegistration )
    {
        const bool bInvalidRegistration = pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pListener == nullptr;
        if ( bInvalidRegistration )
            return false;
        return getAudioRenderListenerRegistrations().addLogged( pRegistration, "audio render listener", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void AudioRenderListenerRegistry::unregisterListener( const AudioRenderListenerRegistration* pRegistration )
    {
        (void)getAudioRenderListenerRegistrations().remove( pRegistration ); // 등록되어 있지 않은 리스너을 빼는 것은 할 일이 없다
    }

    uint32 AudioRenderListenerRegistry::getCount()
    {
        return getAudioRenderListenerRegistrations().getCount();
    }

    void AudioRenderListenerRegistry::notifyRenderedBlock( const float32* pSample, uint32 frameCount, AudioChannelLayout layout )
    {
        const RegistrationList<const AudioRenderListenerRegistration>& registrations = getAudioRenderListenerRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            registrations.getAt( index )->_pListener->onRenderedBlock( pSample, frameCount, layout );
        }
    }

    void AudioRenderListenerRegistry::notifyStreamEnded( AudioChannelLayout layout )
    {
        const RegistrationList<const AudioRenderListenerRegistration>& registrations = getAudioRenderListenerRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            registrations.getAt( index )->_pListener->onStreamEnded( layout );
        }
    }
} // namespace sw
