#include "pch.h"

#include "Engine/Animation/Codec/Acl/AclAnimCodec.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include <acl/compression/compress.h>
#include <acl/compression/track_array.h>
#include <acl/compression/transform_error_metrics.h>
#include <acl/core/iallocator.h>
#include <acl/decompression/decompress.h>

namespace sw
{
    SW_LOG_CALLER( "AclAnimCodec" );

    namespace
    {
        /**
         * @class AclAllocator
         * @brief ACL 의 할당을 sw 할당자로 돌립니다(메모리 태그 · 누수 검사에 보입니다).
         */
        class AclAllocator final : public acl::iallocator
        {
        public:
            void* allocate( size_t size, size_t alignment ) override { return Memory::allocateAligned( size, alignment ); }
            void  deallocate( void* pPointer, size_t size ) override
            {
                (void)size;
                if ( pPointer != nullptr )
                    Memory::freeAligned( pPointer );
            }
        };

        /**
         * @struct AclPoseWriter
         * @brief 압축 해제한 값을 포즈 배열에 바로 씁니다. 트랙 번호가 곧 포즈의 본 번호입니다.
         */
        struct AclPoseWriter final : public acl::track_writer
        {
            AclPoseWriter( Pose& inoutPose, const uint8* pTrackMask )
                : _pTranslation{ inoutPose.getTranslationData() }
                , _pRotation{ inoutPose.getRotationData() }
                , _pScale{ inoutPose.getScaleData() }
                , _pTrackMask{ pTrackMask }
            {
            }

            // 본 LOD — 마스크가 0 인 트랙은 ACL 이 풀지 않는다(언리얼 본 LOD 가 쓰는 같은 자리).
            bool skip_track_rotation( uint32_t trackIndex ) const { return _pTrackMask != nullptr && _pTrackMask[trackIndex] == 0; }
            bool skip_track_translation( uint32_t trackIndex ) const { return _pTrackMask != nullptr && _pTrackMask[trackIndex] == 0; }
            bool skip_track_scale( uint32_t trackIndex ) const { return _pTrackMask != nullptr && _pTrackMask[trackIndex] == 0; }

            void RTM_SIMD_CALL write_rotation( uint32_t trackIndex, rtm::quatf_arg0 rotation )
            {
                _pRotation[trackIndex] = quaternion{ rtm::quat_get_x( rotation ), rtm::quat_get_y( rotation ), rtm::quat_get_z( rotation ), rtm::quat_get_w( rotation ) };
            }

            void RTM_SIMD_CALL write_translation( uint32_t trackIndex, rtm::vector4f_arg0 translation )
            {
                _pTranslation[trackIndex] = float3{ rtm::vector_get_x( translation ), rtm::vector_get_y( translation ), rtm::vector_get_z( translation ) };
            }

            void RTM_SIMD_CALL write_scale( uint32_t trackIndex, rtm::vector4f_arg0 scale )
            {
                _pScale[trackIndex] = float3{ rtm::vector_get_x( scale ), rtm::vector_get_y( scale ), rtm::vector_get_z( scale ) };
            }

            float3*      _pTranslation;
            quaternion*  _pRotation;
            float3*      _pScale;
            const uint8* _pTrackMask;
        };
    } // namespace
} // namespace sw

namespace sw
{
    const AclAnimCodec& AclAnimCodec::getInstance()
    {
        static const AclAnimCodec s_codec;
        return s_codec;
    }

    bool AclAnimCodec::compress( const AnimRawClip& rawClip, const AnimCodecSettings& settings, vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        const uint32 trackCount = rawClip.getTrackCount();
        if ( rawClip._sampleCount == 0 || trackCount == 0 || rawClip._listSample.size() != static_cast<size_t>( rawClip._sampleCount ) * trackCount )
            return false;

        AclAllocator          allocator;
        acl::track_array_qvvf listTrack( allocator, trackCount );
        for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            acl::track_desc_transformf desc{};
            desc.output_index   = trackIndex;
            const int32 parent  = trackIndex < rawClip._listTrackParent.size() ? rawClip._listTrackParent[trackIndex] : -1;
            desc.parent_index   = parent >= 0 ? static_cast<uint32_t>( parent ) : acl::k_invalid_track_index;
            desc.precision      = settings._precision;
            desc.shell_distance = settings._shellDistance;

            acl::track_qvvf track = acl::track_qvvf::make_reserve( desc, allocator, rawClip._sampleCount, rawClip._sampleRate );
            for ( uint32 sampleIndex = 0; sampleIndex < rawClip._sampleCount; ++sampleIndex )
            {
                const BoneTransform& transform = rawClip.getSample( sampleIndex, trackIndex );
                const rtm::quatf     rotation  = rtm::quat_normalize(
                    rtm::quat_set( transform._rotation._x, transform._rotation._y, transform._rotation._z, transform._rotation._w ) );
                const rtm::vector4f translation = rtm::vector_set( transform._translation._x, transform._translation._y, transform._translation._z );
                const rtm::vector4f scale       = rtm::vector_set( transform._scale._x, transform._scale._y, transform._scale._z );
                track[sampleIndex]              = rtm::qvv_set( rotation, translation, scale );
            }
            listTrack[trackIndex] = std::move( track );
        }

        acl::qvvf_transform_error_metric errorMetric;
        acl::compression_settings        compressionSettings = acl::get_default_compression_settings();
        compressionSettings.error_metric                     = &errorMetric;

        acl::output_stats       stats;
        acl::compressed_tracks* pCompressed = nullptr;
        const acl::error_result result      = acl::compress_track_list( allocator, listTrack, compressionSettings, pCompressed, stats );
        if ( result.any() || pCompressed == nullptr )
        {
            SW_LOG_ERROR( "ACL compression failed: %#", result.c_str() );
            return false;
        }
        const uint32 size  = pCompressed->get_size();
        const uint8* pData = reinterpret_cast<const uint8*>( pCompressed );
        outBytes.assign( pData, pData + size );
        allocator.deallocate( pCompressed, size );
        return true;
    }

    bool AclAnimCodec::sample( const uint8* pBytes, size_t byteCount, float32 time, Pose& outPose, const uint8* pTrackMask ) const
    {
        if ( pBytes == nullptr || byteCount == 0 || ( reinterpret_cast<uintptr_t>( pBytes ) & 15u ) != 0 )
            return false;
        const acl::compressed_tracks* pTracks = acl::make_compressed_tracks( pBytes );
        if ( pTracks == nullptr || pTracks->get_size() != byteCount )
            return false;

        acl::decompression_context<acl::default_transform_decompression_settings> context;
        if ( context.initialize( *pTracks ) == false )
            return false;
        outPose.resize( pTracks->get_num_tracks() );
        outPose.setToIdentity();
        context.seek( time, acl::sample_rounding_policy::none );
        AclPoseWriter writer{ outPose, pTrackMask };
        context.decompress_tracks( writer );
        return true;
    }
} // namespace sw
