#include "pch.h"

#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct RawAnimCodecInternal
        {
            /** @brief 머리 크기(표본 수 · 표본율 · 트랙 수)입니다. */
            static constexpr size_t kHeaderSize = 12;
            /** @brief 표본 하나의 트랙 하나가 차지하는 float 수입니다. */
            static constexpr uint32 kFloatPerTransform = 10;

            static void appendFloat( vector<uint8>& outBytes, float32 value )
            {
                uint8 arrByte[sizeof( float32 )]{};
                Memory::copy( arrByte, &value, sizeof( value ) );
                outBytes.insert( outBytes.end(), arrByte, arrByte + sizeof( arrByte ) );
            }

            static void appendUint( vector<uint8>& outBytes, uint32 value )
            {
                uint8 arrByte[sizeof( uint32 )]{};
                Memory::copy( arrByte, &value, sizeof( value ) );
                outBytes.insert( outBytes.end(), arrByte, arrByte + sizeof( arrByte ) );
            }

            static BoneTransform readTransform( const uint8* pData )
            {
                float32 arrValue[kFloatPerTransform]{};
                Memory::copy( arrValue, pData, sizeof( arrValue ) );
                BoneTransform transform{};
                transform._translation = float3{ arrValue[0], arrValue[1], arrValue[2] };
                transform._rotation    = quaternion{ arrValue[3], arrValue[4], arrValue[5], arrValue[6] };
                transform._scale       = float3{ arrValue[7], arrValue[8], arrValue[9] };
                return transform;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const RawAnimCodec& RawAnimCodec::getInstance()
    {
        static const RawAnimCodec s_codec;
        return s_codec;
    }

    bool RawAnimCodec::compress( const AnimRawClip& rawClip, const AnimCodecSettings& settings, vector<uint8>& outBytes ) const
    {
        (void)settings;
        outBytes.clear();
        const uint32 trackCount = rawClip.getTrackCount();
        if ( rawClip._sampleCount == 0 || trackCount == 0 || rawClip._listSample.size() != static_cast<size_t>( rawClip._sampleCount ) * trackCount )
            return false;

        outBytes.reserve( RawAnimCodecInternal::kHeaderSize + rawClip._listSample.size() * RawAnimCodecInternal::kFloatPerTransform * sizeof( float32 ) );
        RawAnimCodecInternal::appendUint( outBytes, rawClip._sampleCount );
        RawAnimCodecInternal::appendFloat( outBytes, rawClip._sampleRate );
        RawAnimCodecInternal::appendUint( outBytes, trackCount );
        for ( const BoneTransform& transform : rawClip._listSample )
        {
            const float32 arrValue[RawAnimCodecInternal::kFloatPerTransform] = { transform._translation._x, transform._translation._y, transform._translation._z,
                                                                                 transform._rotation._x, transform._rotation._y, transform._rotation._z,
                                                                                 transform._rotation._w, transform._scale._x, transform._scale._y,
                                                                                 transform._scale._z };
            for ( const float32 value : arrValue )
                RawAnimCodecInternal::appendFloat( outBytes, value );
        }
        return true;
    }

    bool RawAnimCodec::sample( const uint8* pBytes, size_t byteCount, float32 time, Pose& outPose, const uint8* pTrackMask ) const
    {
        if ( pBytes == nullptr || byteCount < RawAnimCodecInternal::kHeaderSize )
            return false;
        uint32  sampleCount = 0;
        float32 sampleRate  = 0.0f;
        uint32  trackCount  = 0;
        Memory::copy( &sampleCount, pBytes, sizeof( sampleCount ) );
        Memory::copy( &sampleRate, pBytes + 4, sizeof( sampleRate ) );
        Memory::copy( &trackCount, pBytes + 8, sizeof( trackCount ) );
        const size_t transformSize = RawAnimCodecInternal::kFloatPerTransform * sizeof( float32 );
        const size_t expectedSize  = RawAnimCodecInternal::kHeaderSize + static_cast<size_t>( sampleCount ) * trackCount * transformSize;
        if ( sampleCount == 0 || expectedSize != byteCount )
            return false;

        outPose.resize( trackCount );
        if ( pTrackMask != nullptr )
            outPose.setToIdentity();
        const float32 frame        = MathUtil::clamp( time * sampleRate, 0.0f, static_cast<float32>( sampleCount - 1 ) );
        const uint32  firstSample  = static_cast<uint32>( frame );
        const uint32  secondSample = MathUtil::min( firstSample + 1, sampleCount - 1 );
        const float32 alpha        = frame - static_cast<float32>( firstSample );
        const uint8*  pFirst       = pBytes + RawAnimCodecInternal::kHeaderSize + static_cast<size_t>( firstSample ) * trackCount * transformSize;
        const uint8*  pSecond      = pBytes + RawAnimCodecInternal::kHeaderSize + static_cast<size_t>( secondSample ) * trackCount * transformSize;
        for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            if ( pTrackMask != nullptr && pTrackMask[trackIndex] == 0 )
                continue;
            const BoneTransform first  = RawAnimCodecInternal::readTransform( pFirst + trackIndex * transformSize );
            const BoneTransform second = RawAnimCodecInternal::readTransform( pSecond + trackIndex * transformSize );
            outPose.setBoneTransform( trackIndex, BoneTransform::blend( first, second, alpha ) );
        }
        return true;
    }
} // namespace sw
