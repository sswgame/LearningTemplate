#include "pch.h"

#include "Engine/Audio/AudioDownmix.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Audio/AudioChannelLayout.h"

namespace sw
{
    SW_LOG_CALLER( "AudioDownmix" );

    namespace
    {
        struct AudioDownmixInternal
        {
            /** @brief 스피커 하나의 다운믹스 계수입니다. */
            struct SpeakerGain
            {
                uint32  _speakerBit;
                float32 _left;
                float32 _right;
            };

            /** @brief ITU-R BS.775 스테레오 다운믹스 계수입니다. 표에 없는 스피커(높이 채널)는 다루지 않습니다. */
            static constexpr SpeakerGain kArrSpeakerGain[] = {
                {         AudioChannelLayoutUtil::kSpeakerFrontLeft,                1.0f,                0.0f},
                {        AudioChannelLayoutUtil::kSpeakerFrontRight,                0.0f,                1.0f},
                {       AudioChannelLayoutUtil::kSpeakerFrontCenter, MathUtil::kInvSqrt2, MathUtil::kInvSqrt2},
                {      AudioChannelLayoutUtil::kSpeakerLowFrequency,                0.0f,                0.0f},
                {          AudioChannelLayoutUtil::kSpeakerBackLeft, MathUtil::kInvSqrt2,                0.0f},
                {         AudioChannelLayoutUtil::kSpeakerBackRight,                0.0f, MathUtil::kInvSqrt2},
                { AudioChannelLayoutUtil::kSpeakerFrontLeftOfCenter,                1.0f,                0.0f},
                {AudioChannelLayoutUtil::kSpeakerFrontRightOfCenter,                0.0f,                1.0f},
                {        AudioChannelLayoutUtil::kSpeakerBackCenter,                0.5f,                0.5f},
                {          AudioChannelLayoutUtil::kSpeakerSideLeft, MathUtil::kInvSqrt2,                0.0f},
                {         AudioChannelLayoutUtil::kSpeakerSideRight,                0.0f, MathUtil::kInvSqrt2},
            };

            /** @brief 스피커의 계수를 찾습니다. 표에 없으면 nullptr 입니다. */
            static const SpeakerGain* findSpeakerGain( uint32 speakerBit )
            {
                for ( const SpeakerGain& gain : kArrSpeakerGain )
                {
                    if ( gain._speakerBit == speakerBit )
                        return &gain;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AudioDownmix::makeForSpeakers( const uint32* pSpeakerBit, uint32 channelCount, AudioDownmix& outDownmix )
    {
        const bool bChannelCountInRange = 3 <= channelCount && channelCount <= kMaxSourceChannelCount;
        if ( bChannelCountInRange == false || pSpeakerBit == nullptr )
        {
            SW_LOG_ERROR( "Downmix needs 3 to %# source channels (got %#)", kMaxSourceChannelCount, channelCount );
            return false;
        }
        AudioDownmix downmix;
        uint32       usedMask = 0;
        for ( uint32 channelIndex = 0; channelIndex < channelCount; ++channelIndex )
        {
            const uint32                                   speakerBit = pSpeakerBit[channelIndex];
            const AudioDownmixInternal::SpeakerGain* const pGain      = AudioDownmixInternal::findSpeakerGain( speakerBit );
            const bool                                     bRepeated  = ( usedMask & speakerBit ) != 0;
            if ( pGain == nullptr || bRepeated )
            {
                SW_LOG_ERROR( "Downmix cannot place source channel %# (speaker mask bit 0x%#)", channelIndex, Fmt( speakerBit, Format().hex() ) );
                return false;
            }
            usedMask |= speakerBit;
            downmix._arrLeft[channelIndex]  = pGain->_left;
            downmix._arrRight[channelIndex] = pGain->_right;
        }
        downmix._channelCount = channelCount;
        outDownmix            = downmix;
        return true;
    }

    bool AudioDownmix::makeForWaveMask( uint32 channelCount, uint32 channelMask, AudioDownmix& outDownmix )
    {
        const bool bChannelCountInRange = 3 <= channelCount && channelCount <= kMaxSourceChannelCount;
        if ( bChannelCountInRange == false )
        {
            SW_LOG_ERROR( "Downmix needs 3 to %# source channels (got %#)", kMaxSourceChannelCount, channelCount );
            return false;
        }
        // 마스크가 없으면(WAVEFORMATEX) 마스크 비트 순서로 아래부터 채운다.
        const uint32 mask = channelMask == 0 ? ( ( 1u << channelCount ) - 1u ) : channelMask;
        uint32       arrSpeakerBit[kMaxSourceChannelCount]{};
        uint32       assigned = 0;
        for ( uint32 bitIndex = 0; bitIndex < 32 && assigned < channelCount; ++bitIndex )
        {
            const uint32 bit = 1u << bitIndex;
            if ( ( mask & bit ) == 0 )
                continue;
            arrSpeakerBit[assigned] = bit;
            ++assigned;
        }
        if ( assigned < channelCount )
        {
            SW_LOG_ERROR( "Channel mask 0x%# places only %# of %# channels", Fmt( channelMask, Format().hex() ), assigned, channelCount );
            return false;
        }
        return makeForSpeakers( arrSpeakerBit, channelCount, outDownmix );
    }

    bool AudioDownmix::getVorbisSpeakers( uint32 channelCount, uint32 ( &outArrSpeakerBit )[kMaxSourceChannelCount] )
    {
        using Util            = AudioChannelLayoutUtil;
        constexpr uint32 kFL  = Util::kSpeakerFrontLeft;
        constexpr uint32 kFR  = Util::kSpeakerFrontRight;
        constexpr uint32 kFC  = Util::kSpeakerFrontCenter;
        constexpr uint32 kLFE = Util::kSpeakerLowFrequency;
        constexpr uint32 kBL  = Util::kSpeakerBackLeft;
        constexpr uint32 kBR  = Util::kSpeakerBackRight;
        constexpr uint32 kBC  = Util::kSpeakerBackCenter;
        constexpr uint32 kSL  = Util::kSpeakerSideLeft;
        constexpr uint32 kSR  = Util::kSpeakerSideRight;
        // Vorbis I 명세 4.3.9 의 채널 순서다(채널 수마다 하나).
        constexpr uint32 kArrOrder[kMaxSourceChannelCount][kMaxSourceChannelCount] = {
            {kFC,   0,   0,   0,   0,    0,    0,    0},
            {kFL, kFR,   0,   0,   0,    0,    0,    0},
            {kFL, kFC, kFR,   0,   0,    0,    0,    0},
            {kFL, kFR, kBL, kBR,   0,    0,    0,    0},
            {kFL, kFC, kFR, kBL, kBR,    0,    0,    0},
            {kFL, kFC, kFR, kBL, kBR, kLFE,    0,    0},
            {kFL, kFC, kFR, kSL, kSR,  kBC, kLFE,    0},
            {kFL, kFC, kFR, kSL, kSR,  kBL,  kBR, kLFE},
        };
        const bool bChannelCountInRange = 1 <= channelCount && channelCount <= kMaxSourceChannelCount;
        if ( bChannelCountInRange == false )
            return false;
        for ( uint32 channelIndex = 0; channelIndex < kMaxSourceChannelCount; ++channelIndex )
        {
            outArrSpeakerBit[channelIndex] = kArrOrder[channelCount - 1][channelIndex];
        }
        return true;
    }

    void AudioDownmix::apply( const float32* pFrame, float32& outLeft, float32& outRight ) const
    {
        float32 left  = 0.0f;
        float32 right = 0.0f;
        for ( uint32 channelIndex = 0; channelIndex < _channelCount; ++channelIndex )
        {
            left += pFrame[channelIndex] * _arrLeft[channelIndex];
            right += pFrame[channelIndex] * _arrRight[channelIndex];
        }
        outLeft  = left;
        outRight = right;
    }
} // namespace sw
