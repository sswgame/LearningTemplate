#include "pch.h"

#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/ByteStream.h"

namespace sw
{
    namespace
    {
        struct RawAnimationCodecInternal
        {
            /** @brief 헤더 크기입니다(샘플 수, 샘플율, 트랙 수). */
            static constexpr size_t kHeaderSize = 12;
            /** @brief 표본 하나의 트랙 하나가 차지하는 float 수입니다. */
            static constexpr uint32 kFloatPerTransform = 10;

            /** @brief 표본 하나의 트랙 하나(리틀 엔디언 float 10 개)를 읽습니다. 범위는 부르는 쪽이 전체 길이로 먼저 확인했다. */
            static BoneTransform readTransform( const uint8* pData )
            {
                float32 arrValue[kFloatPerTransform]{};
                for ( uint32 valueIndex = 0; valueIndex < kFloatPerTransform; ++valueIndex )
                    arrValue[valueIndex] = LittleEndianUtil::readFloat32( pData + valueIndex * sizeof( float32 ) );
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
    const RawAnimationCodec& RawAnimationCodec::getInstance()
    {
        static const RawAnimationCodec s_codec;
        return s_codec;
    }

    bool RawAnimationCodec::compress( const AnimationRawClip& rawClip, const AnimationCodecSettings& settings, vector<uint8>& outBytes ) const
    {
        (void)settings;
        outBytes.clear();
        const uint32 trackCount      = rawClip.getTrackCount();
        const bool   bInvalidRawClip = rawClip._sampleCount == 0 || trackCount == 0 || rawClip._listSample.size() != static_cast<size_t>( rawClip._sampleCount ) * trackCount;
        if ( bInvalidRawClip )
            return false;

        outBytes.reserve( RawAnimationCodecInternal::kHeaderSize + rawClip._listSample.size() * RawAnimationCodecInternal::kFloatPerTransform * sizeof( float32 ) );
        ByteWriter writer{ outBytes };
        writer.writeUint32( rawClip._sampleCount );
        writer.writeFloat32( rawClip._sampleRate );
        writer.writeUint32( trackCount );
        for ( const BoneTransform& transform : rawClip._listSample )
        {
            const float32 arrValue[RawAnimationCodecInternal::kFloatPerTransform] = { transform._translation._x, transform._translation._y, transform._translation._z,
                                                                                      transform._rotation._x, transform._rotation._y, transform._rotation._z,
                                                                                      transform._rotation._w, transform._scale._x, transform._scale._y,
                                                                                      transform._scale._z };
            for ( const float32 value : arrValue )
            {
                writer.writeFloat32( value );
            }
        }
        return true;
    }

    bool RawAnimationCodec::sample( const uint8* pBytes, size_t byteCount, float32 time, Pose& outPose, const uint8* pTrackMask ) const
    {
        if ( pBytes == nullptr || byteCount < RawAnimationCodecInternal::kHeaderSize )
            return false;
        ByteReader    header{ pBytes, RawAnimationCodecInternal::kHeaderSize };
        const uint32  sampleCount   = header.readUint32();
        const float32 sampleRate    = header.readFloat32();
        const uint32  trackCount    = header.readUint32();
        const size_t  transformSize = RawAnimationCodecInternal::kFloatPerTransform * sizeof( float32 );
        // 곱해서 맞추지 않는다 — 샘플 수 × 트랙 수 × 40 은 64 비트로 넘쳐 작은 바이트와 "맞아" 보이고, 아래 표본 자리가 버퍼 밖이 된다.
        const size_t bodySize = byteCount - RawAnimationCodecInternal::kHeaderSize;
        if ( header.hasOverflowed() || sampleCount == 0 || bodySize % transformSize != 0 || bodySize / transformSize != static_cast<uint64>( sampleCount ) * trackCount )
            return false;

        outPose.resize( trackCount ); // NOLINT(sw-unchecked-count): 위에서 본문 크기로 샘플 수 × 트랙 수를 맞췄다
        if ( pTrackMask != nullptr )
            outPose.setToIdentity();
        const float32 frame        = MathUtil::clamp( time * sampleRate, 0.0f, static_cast<float32>( sampleCount - 1 ) );
        const uint32  firstSample  = static_cast<uint32>( frame );
        const uint32  secondSample = MathUtil::min( firstSample + 1, sampleCount - 1 );
        const float32 alpha        = frame - static_cast<float32>( firstSample );
        const uint8*  pFirst       = pBytes + RawAnimationCodecInternal::kHeaderSize + static_cast<size_t>( firstSample ) * trackCount * transformSize;
        const uint8*  pSecond      = pBytes + RawAnimationCodecInternal::kHeaderSize + static_cast<size_t>( secondSample ) * trackCount * transformSize;
        for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            if ( pTrackMask != nullptr && pTrackMask[trackIndex] == 0 )
                continue;
            const BoneTransform first  = RawAnimationCodecInternal::readTransform( pFirst + trackIndex * transformSize );
            const BoneTransform second = RawAnimationCodecInternal::readTransform( pSecond + trackIndex * transformSize );
            outPose.setBoneTransform( trackIndex, BoneTransform::blend( first, second, alpha ) );
        }
        return true;
    }
} // namespace sw
