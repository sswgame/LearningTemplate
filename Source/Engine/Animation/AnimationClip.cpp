#include "pch.h"

#include "Engine/Animation/AnimationClip.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Serialization/ByteStream.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Content/AssetIO.h"

namespace sw
{
    namespace
    {
        struct AnimationClipInternal
        {
            /** @brief 파일 머리의 매직(`SWAC`)입니다. */
            static constexpr uint8 kArrMagic[4] = { 'S', 'W', 'A', 'C' };
            /** @brief 플래그 비트입니다. 반복을 나타냅니다. */
            static constexpr uint32 kFlagLoop = 1u;
            /** @brief 문자열 하나의 길이 상한입니다. 깨진 파일이 거대한 할당을 일으키지 않게 합니다. */
            static constexpr uint32 kMaxStringLength = 4096;

            /** @brief 리틀 엔디언 바이트 쓰개입니다. */
            struct Writer
            {
                vector<uint8>& _bytes;

                void writeRaw( const void* pData, size_t size )
                {
                    const uint8* pByte = static_cast<const uint8*>( pData );
                    _bytes.insert( _bytes.end(), pByte, pByte + size );
                }
                void writeUint( uint32 value ) { writeRaw( &value, sizeof( value ) ); }
                void writeInt( int32 value ) { writeRaw( &value, sizeof( value ) ); }
                void writeFloat( float32 value ) { writeRaw( &value, sizeof( value ) ); }
                void writeString( string_view text )
                {
                    writeUint( static_cast<uint32>( text.size() ) );
                    writeRaw( text.data(), text.size() );
                }
            };

            /** @brief 길이 붙은 이름을 읽습니다. 상한을 넘으면 실패로 표시합니다 — 깨진 파일의 긴 글을 이름 표에 넣지 않습니다. */
            static hashed_string readName( ByteReader& reader )
            {
                string_view text;
                if ( reader.readStringView( text ) == false )
                    return hashed_string{};
                if ( text.size() > kMaxStringLength )
                {
                    reader.markFailed();
                    return hashed_string{};
                }
                return hashed_string( string( text ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 AnimationClipCurve::evaluate( float32 time ) const
    {
        if ( _listKey.empty() )
            return 0.0f;
        if ( time <= _listKey.front()._time )
            return _listKey.front()._value;
        for ( size_t keyIndex = 1; keyIndex < _listKey.size(); ++keyIndex )
        {
            const AnimationClipCurveKey& next = _listKey[keyIndex];
            if ( time > next._time )
                continue;
            const AnimationClipCurveKey& previous = _listKey[keyIndex - 1];
            const float32                span     = next._time - previous._time;
            const float32                alpha    = span > 0.0f ? ( time - previous._time ) / span : 1.0f;
            return MathUtil::lerp( previous._value, next._value, alpha );
        }
        return _listKey.back()._value;
    }

    AnimationClip::AnimationClip()
        : _name{}
        , _listTrackName{}
        , _listCodecBlock{}
        , _codecByteCount{ 0 }
        , _notifyTrack{}
        , _listCurve{}
        , _duration{ 0.0f }
        , _rootMotionTrack{ constant::invalid_index::kInt32 }
        , _codecID{ AnimationCodecID::Raw }
        , _bLoop{ SW_TRUE }
    {
    }

    const AnimationClipCurve* AnimationClip::findCurve( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&AnimationClipCurve::_name>( _listCurve, name );
    }

    bool AnimationClip::compressFrom( const AnimationRawClip& rawClip, const IAnimationCodec& codec, const AnimationCodecSettings& settings, AnimationCodecStats* pOutStats )
    {
        vector<uint8>       bytes;
        AnimationCodecStats stats{};
        if ( AnimationCodecRegistry::compressAndMeasure( codec, rawClip, settings, bytes, stats ) == false )
            return false;
        _listTrackName = rawClip._listTrackName;
        _duration      = rawClip.getDuration();
        _codecID       = codec.getID();
        assignCodecBytes( bytes.data(), bytes.size() );
        if ( pOutStats != nullptr )
            *pOutStats = stats;
        return true;
    }

    bool AnimationClip::sampleTracks( float32 time, Pose& outTrackPose, const uint8* pTrackMask ) const
    {
        const IAnimationCodec* pCodec = AnimationCodecRegistry::findCodec( _codecID );
        if ( pCodec == nullptr || _codecByteCount == 0 )
            return false;
        return pCodec->sample( getCodecData(), _codecByteCount, MathUtil::clamp( time, 0.0f, _duration ), outTrackPose, pTrackMask );
    }

    bool AnimationClip::makeTrackToBoneMap( const Skeleton& skeleton, vector<int32>& outListBone ) const
    {
        outListBone.resize( _listTrackName.size() );
        uint32               missingCount  = 0;
        const hashed_string* pFirstMissing = nullptr;
        for ( size_t trackIndex = 0; trackIndex < _listTrackName.size(); ++trackIndex )
        {
            outListBone[trackIndex] = Skeleton::toBoneSlot( skeleton.findBoneIndex( _listTrackName[trackIndex] ) );
            if ( outListBone[trackIndex] >= 0 )
                continue;
            ++missingCount;
            pFirstMissing = pFirstMissing != nullptr ? pFirstMissing : &_listTrackName[trackIndex];
        }
        if ( missingCount == 0 || skeleton.isPartial() )
            return true;
        SW_LOG_ERROR_ID( "AnimationClip.TracksNotInSkeleton", "Clip '%#': %# of %# tracks name bones the skeleton does not have (first: '%#') - wrong skeleton for this clip, or declare the skeleton a partial one", getName().c_str(), missingCount,
                         static_cast<uint32>( _listTrackName.size() ), pFirstMissing->c_str() );
        return false;
    }

    bool AnimationClip::samplePose( const Skeleton& skeleton, float32 time, const vector<int32>& listTrackToBone, Pose& inoutPose, Pose& scratchTrackPose, bool bAnchorRootMotion,
                                    const uint8* pTrackMask ) const
    {
        if ( sampleTracks( time, scratchTrackPose, pTrackMask ) == false )
            return false;
        copyTracksToPose( skeleton, scratchTrackPose, listTrackToBone, inoutPose, pTrackMask );
        // 루트 모션을 추출하면 그 본은 클립 시작 위치에 고정한다. 움직임은 오브젝트가 맡는다.
        const int32 rootTrack              = _rootMotionTrack;
        const bool  bRootMotionNotAnchored = bAnchorRootMotion == false || rootTrack < 0 || static_cast<size_t>( rootTrack ) >= listTrackToBone.size() || listTrackToBone[static_cast<size_t>( rootTrack )] < 0;
        if ( bRootMotionNotAnchored )
            return true;
        if ( pTrackMask != nullptr && pTrackMask[rootTrack] == 0 )
            return true;
        const uint32        rootBone = static_cast<uint32>( listTrackToBone[static_cast<size_t>( rootTrack )] );
        BoneTransform       anchored = inoutPose.getBoneTransform( rootBone );
        const BoneTransform anchor   = skeleton.applyClipRule( rootBone, getRootMotionAnchor() );
        anchored._translation        = anchor._translation;
        anchored._rotation           = anchor._rotation;
        inoutPose.setBoneTransform( rootBone, anchored );
        return true;
    }

    void AnimationClip::copyTracksToPose( const Skeleton& skeleton, const Pose& trackPose, const vector<int32>& listTrackToBone, Pose& inoutPose, const uint8* pTrackMask )
    {
        const uint32 trackCount = MathUtil::min( trackPose.getBoneCount(), static_cast<uint32>( listTrackToBone.size() ) );
        const bool   bClipRules = skeleton.hasClipRules(); // 대부분의 스켈레톤은 규칙이 없다 — 트랙마다 규칙 표를 보지 않는다
        for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            const int32 boneIndex = listTrackToBone[trackIndex];
            if ( boneIndex < 0 || ( pTrackMask != nullptr && pTrackMask[trackIndex] == 0 ) )
                continue;
            const BoneTransform sampled = trackPose.getBoneTransform( trackIndex );
            inoutPose.setBoneTransform( static_cast<uint32>( boneIndex ), bClipRules ? skeleton.applyClipRule( static_cast<uint32>( boneIndex ), sampled ) : sampled );
        }
    }

    BoneTransform AnimationClip::sampleRootMotionTrack( float32 time ) const
    {
        Pose trackPose;
        if ( _rootMotionTrack < 0 || sampleTracks( time, trackPose ) == false )
            return BoneTransform{};
        return trackPose.getBoneTransform( static_cast<uint32>( _rootMotionTrack ) );
    }

    BoneTransform AnimationClip::getRootMotionAnchor() const
    {
        return sampleRootMotionTrack( 0.0f );
    }

    BoneTransform AnimationClip::computeRootMotionDelta( const AnimationTimeStep& step ) const
    {
        BoneTransform delta{};
        if ( _rootMotionTrack < 0 )
            return delta;

        const BoneTransform previous = sampleRootMotionTrack( step._previousTime );
        const BoneTransform current  = sampleRootMotionTrack( step._currentTime );
        if ( step._wrapCount == 0 )
        {
            delta._translation = current._translation - previous._translation;
            delta._rotation    = ( previous._rotation.inverse() * current._rotation ).normalize();
            return delta;
        }

        // 반복 경계를 넘었다. 끝까지 간 구간, 온 바퀴들, 처음부터 현재까지를 이어서 합친다.
        const BoneTransform start    = sampleRootMotionTrack( 0.0f );
        const BoneTransform end      = sampleRootMotionTrack( _duration );
        const float3        loopMove = end._translation - start._translation;
        const quaternion    loopTurn = ( start._rotation.inverse() * end._rotation ).normalize();
        delta._translation           = ( end._translation - previous._translation ) + ( current._translation - start._translation );
        quaternion rotation          = ( previous._rotation.inverse() * end._rotation ).normalize();
        for ( uint32 loopIndex = 1; loopIndex < step._wrapCount; ++loopIndex )
        {
            delta._translation = delta._translation + loopMove;
            rotation           = ( rotation * loopTurn ).normalize();
        }
        delta._rotation = ( rotation * ( start._rotation.inverse() * current._rotation ) ).normalize();
        return delta;
    }

    void AnimationClip::assignCodecBytes( const uint8* pData, size_t byteCount )
    {
        _listCodecBlock.clear();
        _listCodecBlock.resize( ( byteCount + sizeof( AnimationCodecBlock ) - 1 ) / sizeof( AnimationCodecBlock ) );
        if ( byteCount > 0 )
            Memory::copy( _listCodecBlock.data(), pData, byteCount );
        _codecByteCount = byteCount;
    }

    void AnimationClip::clear()
    {
        *this = AnimationClip{};
    }

    void AnimationClip::makeBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        AnimationClipInternal::Writer writer{ outBytes };
        writer.writeRaw( AnimationClipInternal::kArrMagic, sizeof( AnimationClipInternal::kArrMagic ) );
        writer.writeUint( kVersion );
        writer.writeUint( _bLoop == SW_TRUE ? AnimationClipInternal::kFlagLoop : 0u );
        writer.writeFloat( _duration );
        writer.writeString( _name.c_str() );
        writer.writeUint( static_cast<uint32>( _listTrackName.size() ) );
        for ( const hashed_string& trackName : _listTrackName )
        {
            writer.writeString( trackName.c_str() );
        }
        writer.writeInt( _rootMotionTrack );
        writer.writeUint( static_cast<uint32>( _codecID ) );
        writer.writeUint( static_cast<uint32>( _codecByteCount ) );
        writer.writeRaw( getCodecData(), _codecByteCount );
        writer.writeUint( static_cast<uint32>( _notifyTrack.getEvents().size() ) );
        for ( const AnimationNotifyEvent& event : _notifyTrack.getEvents() )
        {
            writer.writeString( event._name.c_str() );
            writer.writeFloat( event._time );
            writer.writeFloat( event._duration );
        }
        writer.writeUint( static_cast<uint32>( _listCurve.size() ) );
        for ( const AnimationClipCurve& curve : _listCurve )
        {
            writer.writeString( curve._name.c_str() );
            writer.writeUint( static_cast<uint32>( curve._listKey.size() ) );
            for ( const AnimationClipCurveKey& key : curve._listKey )
            {
                writer.writeFloat( key._time );
                writer.writeFloat( key._value );
            }
        }
    }

    bool AnimationClip::readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel )
    {
        clear();
        const bool bMissingClipMagic = pData == nullptr || byteCount < sizeof( AnimationClipInternal::kArrMagic ) ||
                                       Memory::compare( pData, AnimationClipInternal::kArrMagic, sizeof( AnimationClipInternal::kArrMagic ) ) != 0;
        if ( bMissingClipMagic )
        {
            SW_LOG_ERROR_ID( "AnimationClip.AnimationClipNoSWAC", "Animation clip '%#' has no SWAC magic", sourceLabel );
            return false;
        }

        ByteReader reader{ pData, byteCount };
        (void)reader.skipBytes( sizeof( AnimationClipInternal::kArrMagic ) ); // 위에서 길이와 매직을 봤다
        const uint32 version = reader.readUint32();
        if ( version != kVersion )
        {
            SW_LOG_ERROR_ID( "AnimationClip.AnimationClipVersionNot", "Animation clip '%#' is version %#, not the current %# - re-import the model", sourceLabel, version, kVersion );
            return false;
        }
        const uint32 flags = reader.readUint32();
        _bLoop             = ( flags & AnimationClipInternal::kFlagLoop ) != 0 ? SW_TRUE : SW_FALSE;
        _duration          = reader.readFloat32();
        _name              = AnimationClipInternal::readName( reader );
        // 실패는 끈적하다 — 아래 끝의 `isFullyRead` 가 한 번에 본다.
        (void)reader.readCountedList( _listTrackName, sizeof( uint32 ), [&]( hashed_string& trackName )
        {
            trackName = AnimationClipInternal::readName( reader );
        } );
        _rootMotionTrack           = static_cast<int32>( reader.readUint32() );
        const uint32 codecValue    = reader.readUint32();
        const uint32 codecByteSize = reader.readUint32();
        const uint8* pCodecBytes   = nullptr;
        if ( reader.readView( codecByteSize, pCodecBytes ) )
            assignCodecBytes( pCodecBytes, codecByteSize );
        const uint32 notifyCount = reader.readUint32();
        if ( reader.canHold( notifyCount, sizeof( uint32 ) * 3 ) )
        {
            for ( uint32 notifyIndex = 0; notifyIndex < notifyCount && reader.hasOverflowed() == false; ++notifyIndex )
            {
                AnimationNotifyEvent event{};
                event._name     = AnimationClipInternal::readName( reader );
                event._time     = reader.readFloat32();
                event._duration = reader.readFloat32();
                _notifyTrack.addEvent( event );
            }
        }
        (void)reader.readCountedList( _listCurve, sizeof( uint32 ) * 2, [&]( AnimationClipCurve& curve ) -> bool
        {
            curve._name = AnimationClipInternal::readName( reader );
            return reader.readCountedList( curve._listKey, sizeof( float32 ) * 2, [&]( AnimationClipCurveKey& key )
            {
                key._time  = reader.readFloat32();
                key._value = reader.readFloat32();
            } );
        } );

        const bool bCodecKnown    = codecValue < static_cast<uint32>( AnimationCodecID::Count );
        const bool bRootInRange   = _rootMotionTrack >= constant::invalid_index::kInt32 && _rootMotionTrack < static_cast<int32>( _listTrackName.size() );
        const bool bValidHeader   = _duration >= 0.0f;
        const bool bMalformedClip = reader.isFullyRead() == false || bCodecKnown == false || bRootInRange == false || bValidHeader == false;
        if ( bMalformedClip )
        {
            SW_LOG_ERROR_ID( "AnimationClip.AnimationClipMalformed", "Animation clip '%#' is malformed", sourceLabel );
            clear();
            return false;
        }
        _codecID = static_cast<AnimationCodecID>( codecValue );
        return true;
    }

    bool AnimationClip::saveToFile( const AbsolutePath& absPath ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( absPath.str() ) == false )
            return false;
        return FileUtil::writeFile( absPath.str(), bytes.data(), bytes.size() );
    }

    bool AnimationClip::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Animation );
        vector<uint8> bytes;
        if ( AssetIO::readBinaryAsset( assetID, bytes ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationClip.AnimationClipCouldNot", "Animation clip '%#' could not be read", path );
            clear();
            return false;
        }
        return readFromBytes( bytes.data(), bytes.size(), path );
    }
} // namespace sw
