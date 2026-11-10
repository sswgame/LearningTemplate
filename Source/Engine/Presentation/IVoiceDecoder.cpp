#include "pch.h"

#include "Engine/Presentation/IVoiceDecoder.h"

#include "Core/Registry/RegistrationSlot.h"

namespace sw
{
    namespace
    {
        struct VoiceDecoderInternal
        {
            /** @brief 걸린 창구입니다(이미지에 하나 — 둘째는 거절). 함수 정적이라 등록자의 정적 초기화 순서와 무관하다. */
            static SW_DEFINE_REGISTRATION_SLOT( getSlot, IVoiceDecoder, "VoiceDecoder" )
        };
    } // namespace

    IVoiceDecoder* IVoiceDecoder::find()
    {
        return VoiceDecoderInternal::getSlot().get();
    }

    bool IVoiceDecoder::attach( IVoiceDecoder* pDecoder )
    {
        return VoiceDecoderInternal::getSlot().setLogged( pDecoder, "Voice decoder", "VoiceDecoder" ) == RegistrationResult::Added;
    }

    void IVoiceDecoder::detach( IVoiceDecoder* pDecoder )
    {
        (void)VoiceDecoderInternal::getSlot().reset( pDecoder );
    }
} // namespace sw
