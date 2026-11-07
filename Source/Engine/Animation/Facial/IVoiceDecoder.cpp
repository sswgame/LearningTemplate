#include "pch.h"

#include "Engine/Animation/Facial/IVoiceDecoder.h"

namespace sw
{
    namespace
    {
        struct VoiceDecoderInternal
        {
            /** @brief 등록된 창구입니다. 함수 안 정적 변수라 등록자의 정적 초기화 순서와 무관하다. */
            static IVoiceDecoder*& slot()
            {
                static IVoiceDecoder* s_pDecoder = nullptr;
                return s_pDecoder;
            }
        };
    } // namespace

    IVoiceDecoder* IVoiceDecoder::find()
    {
        return VoiceDecoderInternal::slot();
    }

    IVoiceDecoder::Registration::Registration( IVoiceDecoder& decoder )
    {
        SW_ASSERT( VoiceDecoderInternal::slot() == nullptr ); // 이미지에 하나다 — 둘이면 등록의 잘못이다
        VoiceDecoderInternal::slot() = &decoder;
    }
} // namespace sw
