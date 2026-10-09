#include "pch.h"

#include "Engine/Animation/Codec/AnimCodec.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/Codec/Acl/AclAnimCodec.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"

namespace sw
{
    namespace
    {
        struct AnimCodecInternal
        {
            /** @brief 가상 정점 넷 — 본 원점과 세 축 방향(곱하기 shell 거리)입니다. */
            static constexpr uint32 kVirtualVertexCount = 4;

            /** @brief 등록된 코덱 표입니다. 번호 순서입니다. */
            static const IAnimCodec* const* getCodecTable()
            {
                static const IAnimCodec* const s_arrCodec[static_cast<uint32>( AnimCodecId::Count )] = { &RawAnimCodec::getInstance(), &AclAnimCodec::getInstance() };
                return s_arrCodec;
            }

            /** @brief 모델 공간 행렬로 가상 정점 하나를 옮깁니다. */
            static float3 transformVirtualVertex( const float4x4& model, uint32 vertexIndex, float32 shellDistance )
            {
                const float3 arrOffset[kVirtualVertexCount] = {
                    float3{},
                    float3{ shellDistance, 0.0f, 0.0f },
                    float3{ 0.0f, shellDistance, 0.0f },
                    float3{ 0.0f, 0.0f, shellDistance }
                };
                return float3::transform( arrOffset[vertexIndex], model );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AnimRawClip::sample( float32 time, Pose& outPose ) const
    {
        const uint32 trackCount = getTrackCount();
        outPose.resize( trackCount );
        if ( _sampleCount == 0 || trackCount == 0 )
            return;

        const float32 frame        = MathUtil::clamp( time * _sampleRate, 0.0f, static_cast<float32>( _sampleCount - 1 ) );
        const uint32  firstSample  = static_cast<uint32>( frame );
        const uint32  secondSample = MathUtil::min( firstSample + 1, _sampleCount - 1 );
        const float32 alpha        = frame - static_cast<float32>( firstSample );
        for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            outPose.setBoneTransform( trackIndex, BoneTransform::blend( getSample( firstSample, trackIndex ), getSample( secondSample, trackIndex ), alpha ) );
        }
    }

    const IAnimCodec* AnimCodecRegistry::findCodec( AnimCodecId id )
    {
        const uint32 index = static_cast<uint32>( id );
        return index < static_cast<uint32>( AnimCodecId::Count ) ? AnimCodecInternal::getCodecTable()[index] : nullptr;
    }

    const IAnimCodec* AnimCodecRegistry::findCodecByName( string_view name )
    {
        for ( uint32 index = 0; index < static_cast<uint32>( AnimCodecId::Count ); ++index )
        {
            const IAnimCodec* pCodec = AnimCodecInternal::getCodecTable()[index];
            if ( StringUtil::equals( name, string_view( pCodec->getName() ), true ) )
                return pCodec;
        }
        return nullptr;
    }

    bool AnimCodecRegistry::compressAndMeasure( const IAnimCodec& codec, const AnimRawClip& rawClip, const AnimCodecSettings& settings, vector<uint8>& outBytes,
                                                AnimCodecStats& outStats )
    {
        outStats = AnimCodecStats{};
        if ( codec.compress( rawClip, settings, outBytes ) == false )
            return false;
        outStats._rawByteCount        = rawClip._sampleCount * rawClip.getTrackCount() * 10u * static_cast<uint32>( sizeof( float32 ) );
        outStats._compressedByteCount = static_cast<uint32>( outBytes.size() );
        outStats._maxError            = measureMaxError( codec, rawClip, outBytes.data(), outBytes.size(), settings._shellDistance );
        return true;
    }

    float32 AnimCodecRegistry::measureMaxError( const IAnimCodec& codec, const AnimRawClip& rawClip, const uint8* pBytes, size_t byteCount, float32 shellDistance )
    {
        if ( rawClip._sampleCount == 0 || rawClip.getTrackCount() == 0 )
            return 0.0f;

        // 블롭은 16 바이트 정렬이어야 한다 — 바이트 배열이 그 정렬을 보장하지 않으므로 정렬된 사본에서 잰다.
        const size_t           blockCount = ( byteCount + sizeof( float4 ) - 1 ) / sizeof( float4 );
        vector<AnimCodecBlock> listBlock( blockCount );
        Memory::copy( listBlock.data(), pBytes, byteCount );
        const uint8* pAligned = reinterpret_cast<const uint8*>( listBlock.data() );

        Pose             rawPose;
        Pose             codecPose;
        vector<float4x4> listRawModel;
        vector<float4x4> listCodecModel;
        float32          maxError   = 0.0f;
        const uint32     probeCount = rawClip._sampleCount > 1 ? ( rawClip._sampleCount - 1 ) * 2 + 1 : 1;
        const float32    probeSpan  = 0.5f / MathUtil::max( rawClip._sampleRate, 1.0f );
        for ( uint32 probeIndex = 0; probeIndex < probeCount; ++probeIndex )
        {
            const float32 time = MathUtil::min( static_cast<float32>( probeIndex ) * probeSpan, rawClip.getDuration() );
            rawClip.sample( time, rawPose );
            if ( codec.sample( pAligned, byteCount, time, codecPose ) == false )
                return MathUtil::kMaxFloat;
            rawPose.computeModelSpace( rawClip._listTrackParent, listRawModel );
            codecPose.computeModelSpace( rawClip._listTrackParent, listCodecModel );
            for ( size_t trackIndex = 0; trackIndex < listRawModel.size() && trackIndex < listCodecModel.size(); ++trackIndex )
            {
                for ( uint32 vertexIndex = 0; vertexIndex < AnimCodecInternal::kVirtualVertexCount; ++vertexIndex )
                {
                    const float3 rawVertex   = AnimCodecInternal::transformVirtualVertex( listRawModel[trackIndex], vertexIndex, shellDistance );
                    const float3 codecVertex = AnimCodecInternal::transformVirtualVertex( listCodecModel[trackIndex], vertexIndex, shellDistance );
                    maxError                 = MathUtil::max( maxError, float3::getDistance( rawVertex, codecVertex ) );
                }
            }
        }
        return maxError;
    }
} // namespace sw
