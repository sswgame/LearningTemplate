#include "pch.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioPCM.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Presentation/IVoiceDecoder.h"

namespace sw
{
    namespace
    {
        /**
         * @class AudioVoiceDecoder
         * @brief 립싱크의 음성 디코드 창구(`IVoiceDecoder`)를 클립 디코더(WAV · OGG)로 잇습니다. 상태가 없다.
         */
        class AudioVoiceDecoder final : public IVoiceDecoder
        {
        public:
            bool decodeMono( const AssetID& assetID, vector<float32>& outListSample, uint32& outSampleRate ) override
            {
                vector<uint8> bytes;
                AudioPCM      pcm;
                AudioClipData clip;
                if ( AssetIO::readBinaryAsset( assetID, bytes ) == false || AudioPCM::decode( assetID.view(), bytes.data(), bytes.size(), pcm ) == false ||
                     AudioClipData::convertPCM( pcm, clip ) == false )
                    return false;
                clip.copyMonoSamples( outListSample );
                outSampleRate = clip._sampleRate;
                return true;
            }
        };

        AudioVoiceDecoder                 s_audioVoiceDecoder;
        const IVoiceDecoder::Registration s_audioVoiceDecoderRegistration{ s_audioVoiceDecoder };
    } // namespace
} // namespace sw
