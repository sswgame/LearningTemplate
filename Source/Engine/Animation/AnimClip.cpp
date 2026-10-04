#include "pch.h"

#include "Engine/Animation/AnimClip.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "AnimClip" );

    namespace
    {
        struct AnimClipInternal
        {
            /** @brief 파일 머리의 매직(`SWAC`)입니다. */
            static constexpr uint8 kArrMagic[4] = { 'S', 'W', 'A', 'C' };
            /** @brief 플래그 비트 — 반복입니다. */
            static constexpr uint32 kFlagLoop = 1u;
            /** @brief 문자열 하나의 길이 상한입니다(깨진 파일이 거대한 할당을 부르지 않게). */
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

            /** @brief 범위를 지키는 바이트 읽개입니다. 넘치면 실패 상태가 되고 그 뒤 읽기는 모두 0 입니다. */
            struct Reader
            {
                const uint8* _pData;
                size_t       _size;
                size_t       _offset;
                bool         _bFailed;

                [[nodiscard]] bool readRaw( void* pOut, size_t size )
                {
                    if ( _bFailed || _offset + size > _size )
                    {
                        _bFailed = true;
                        Memory::set( pOut, 0, size );
                        return false;
                    }
                    Memory::copy( pOut, _pData + _offset, size );
                    _offset += size;
                    return true;
                }
                uint32 readUint()
                {
                    uint32 value = 0;
                    (void)readRaw( &value, sizeof( value ) );
                    return value;
                }
                int32 readInt()
                {
                    int32 value = 0;
                    (void)readRaw( &value, sizeof( value ) );
                    return value;
                }
                float32 readFloat()
                {
                    float32 value = 0.0f;
                    (void)readRaw( &value, sizeof( value ) );
                    return value;
                }
                string readString()
                {
                    const uint32 length = readUint();
                    if ( length > kMaxStringLength || _offset + length > _size )
                    {
                        _bFailed = true;
                        return {};
                    }
                    string text( reinterpret_cast<const utf8*>( _pData + _offset ), length );
                    _offset += length;
                    return text;
                }
                /** @brief 원소 수가 남은 바이트로 담길 수 있는지 봅니다(원소 하나가 @p minElementSize 바이트 이상). */
                bool canHold( uint32 count, size_t minElementSize )
                {
                    if ( _bFailed || static_cast<uint64>( count ) * minElementSize > _size - _offset )
                        _bFailed = true;
                    return _bFailed == false;
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 AnimCurve::evaluate( float32 time ) const
    {
        if ( _listKey.empty() )
            return 0.0f;
        if ( time <= _listKey.front()._time )
            return _listKey.front()._value;
        for ( size_t keyIndex = 1; keyIndex < _listKey.size(); ++keyIndex )
        {
            const AnimCurveKey& next = _listKey[keyIndex];
            if ( time > next._time )
                continue;
            const AnimCurveKey& previous = _listKey[keyIndex - 1];
            const float32       span     = next._time - previous._time;
            const float32       alpha    = span > 0.0f ? ( time - previous._time ) / span : 1.0f;
            return MathUtil::lerp( previous._value, next._value, alpha );
        }
        return _listKey.back()._value;
    }

    AnimClip::AnimClip()
        : _name{}
        , _listTrackName{}
        , _listCodecBlock{}
        , _codecByteCount{ 0 }
        , _notifyTrack{}
        , _listCurve{}
        , _duration{ 0.0f }
        , _rootMotionTrack{ -1 }
        , _codecId{ AnimCodecId::Raw }
        , _bLoop{ SW_TRUE }
    {
    }

    const AnimCurve* AnimClip::findCurve( const hashed_string& name ) const
    {
        for ( const AnimCurve& curve : _listCurve )
        {
            if ( curve._name == name )
                return &curve;
        }
        return nullptr;
    }

    bool AnimClip::compressFrom( const AnimRawClip& rawClip, const IAnimCodec& codec, const AnimCodecSettings& settings, AnimCodecStats* pOutStats )
    {
        vector<uint8>  bytes;
        AnimCodecStats stats{};
        if ( AnimCodecRegistry::compressAndMeasure( codec, rawClip, settings, bytes, stats ) == false )
            return false;
        _listTrackName = rawClip._listTrackName;
        _duration      = rawClip.getDuration();
        _codecId       = codec.getId();
        assignCodecBytes( bytes.data(), bytes.size() );
        if ( pOutStats != nullptr )
            *pOutStats = stats;
        return true;
    }

    bool AnimClip::sampleTracks( float32 time, Pose& outTrackPose ) const
    {
        const IAnimCodec* pCodec = AnimCodecRegistry::findCodec( _codecId );
        if ( pCodec == nullptr || _codecByteCount == 0 )
            return false;
        return pCodec->sample( getCodecData(), _codecByteCount, MathUtil::clamp( time, 0.0f, _duration ), outTrackPose );
    }

    void AnimClip::makeTrackToBoneMap( const Skeleton& skeleton, vector<int32>& outListBone ) const
    {
        outListBone.resize( _listTrackName.size() );
        for ( size_t trackIndex = 0; trackIndex < _listTrackName.size(); ++trackIndex )
            outListBone[trackIndex] = skeleton.findBoneIndex( _listTrackName[trackIndex] );
    }

    void AnimClip::copyTracksToPose( const Pose& trackPose, const vector<int32>& listTrackToBone, Pose& inoutPose )
    {
        const uint32 trackCount = MathUtil::min( trackPose.getBoneCount(), static_cast<uint32>( listTrackToBone.size() ) );
        for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
        {
            const int32 boneIndex = listTrackToBone[trackIndex];
            if ( boneIndex >= 0 )
                inoutPose.setBoneTransform( static_cast<uint32>( boneIndex ), trackPose.getBoneTransform( trackIndex ) );
        }
    }

    BoneTransform AnimClip::sampleRootMotionTrack( float32 time ) const
    {
        Pose trackPose;
        if ( _rootMotionTrack < 0 || sampleTracks( time, trackPose ) == false )
            return BoneTransform{};
        return trackPose.getBoneTransform( static_cast<uint32>( _rootMotionTrack ) );
    }

    BoneTransform AnimClip::getRootMotionAnchor() const
    {
        return sampleRootMotionTrack( 0.0f );
    }

    BoneTransform AnimClip::computeRootMotionDelta( const AnimTimeStep& step ) const
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

        // 반복 경계를 넘었다 — 끝까지 간 몫, 온 바퀴들, 처음부터 지금까지를 잇는다.
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

    void AnimClip::assignCodecBytes( const uint8* pData, size_t byteCount )
    {
        _listCodecBlock.clear();
        _listCodecBlock.resize( ( byteCount + sizeof( AnimCodecBlock ) - 1 ) / sizeof( AnimCodecBlock ) );
        if ( byteCount > 0 )
            Memory::copy( _listCodecBlock.data(), pData, byteCount );
        _codecByteCount = byteCount;
    }

    void AnimClip::clear()
    {
        *this = AnimClip{};
    }

    void AnimClip::makeBytes( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        AnimClipInternal::Writer writer{ outBytes };
        writer.writeRaw( AnimClipInternal::kArrMagic, sizeof( AnimClipInternal::kArrMagic ) );
        writer.writeUint( kVersion );
        writer.writeUint( _bLoop == SW_TRUE ? AnimClipInternal::kFlagLoop : 0u );
        writer.writeFloat( _duration );
        writer.writeString( _name.c_str() );
        writer.writeUint( static_cast<uint32>( _listTrackName.size() ) );
        for ( const hashed_string& trackName : _listTrackName )
            writer.writeString( trackName.c_str() );
        writer.writeInt( _rootMotionTrack );
        writer.writeUint( static_cast<uint32>( _codecId ) );
        writer.writeUint( static_cast<uint32>( _codecByteCount ) );
        writer.writeRaw( getCodecData(), _codecByteCount );
        writer.writeUint( static_cast<uint32>( _notifyTrack.getEvents().size() ) );
        for ( const AnimNotifyEvent& event : _notifyTrack.getEvents() )
        {
            writer.writeString( event._name.c_str() );
            writer.writeFloat( event._time );
            writer.writeFloat( event._duration );
        }
        writer.writeUint( static_cast<uint32>( _listCurve.size() ) );
        for ( const AnimCurve& curve : _listCurve )
        {
            writer.writeString( curve._name.c_str() );
            writer.writeUint( static_cast<uint32>( curve._listKey.size() ) );
            for ( const AnimCurveKey& key : curve._listKey )
            {
                writer.writeFloat( key._time );
                writer.writeFloat( key._value );
            }
        }
    }

    bool AnimClip::readFromBytes( const uint8* pData, size_t byteCount, string_view sourceLabel )
    {
        clear();
        if ( pData == nullptr || byteCount < sizeof( AnimClipInternal::kArrMagic ) ||
             Memory::compare( pData, AnimClipInternal::kArrMagic, sizeof( AnimClipInternal::kArrMagic ) ) != 0 )
        {
            SW_LOG_ERROR( "Animation clip '%#' has no SWAC magic", sourceLabel );
            return false;
        }

        AnimClipInternal::Reader reader{ pData, byteCount, sizeof( AnimClipInternal::kArrMagic ), false };
        const uint32             version = reader.readUint();
        if ( version != kVersion )
        {
            SW_LOG_ERROR( "Animation clip '%#' is version %#, not the current %# - re-import the model", sourceLabel, version, kVersion );
            return false;
        }
        const uint32 flags      = reader.readUint();
        _bLoop                  = ( flags & AnimClipInternal::kFlagLoop ) != 0 ? SW_TRUE : SW_FALSE;
        _duration               = reader.readFloat();
        _name                   = hashed_string( reader.readString() );
        const uint32 trackCount = reader.readUint();
        if ( reader.canHold( trackCount, sizeof( uint32 ) ) )
        {
            _listTrackName.reserve( trackCount );
            for ( uint32 trackIndex = 0; trackIndex < trackCount; ++trackIndex )
                _listTrackName.push_back( hashed_string( reader.readString() ) );
        }
        _rootMotionTrack           = reader.readInt();
        const uint32 codecValue    = reader.readUint();
        const uint32 codecByteSize = reader.readUint();
        if ( reader.canHold( codecByteSize, 1 ) )
        {
            assignCodecBytes( pData + reader._offset, codecByteSize );
            reader._offset += codecByteSize;
        }
        const uint32 notifyCount = reader.readUint();
        if ( reader.canHold( notifyCount, sizeof( uint32 ) * 3 ) )
        {
            for ( uint32 notifyIndex = 0; notifyIndex < notifyCount; ++notifyIndex )
            {
                AnimNotifyEvent event{};
                event._name     = hashed_string( reader.readString() );
                event._time     = reader.readFloat();
                event._duration = reader.readFloat();
                _notifyTrack.addEvent( event );
            }
        }
        const uint32 curveCount = reader.readUint();
        if ( reader.canHold( curveCount, sizeof( uint32 ) * 2 ) )
        {
            for ( uint32 curveIndex = 0; curveIndex < curveCount; ++curveIndex )
            {
                AnimCurve curve{};
                curve._name           = hashed_string( reader.readString() );
                const uint32 keyCount = reader.readUint();
                if ( reader.canHold( keyCount, sizeof( float32 ) * 2 ) == false )
                    break;
                curve._listKey.resize( keyCount );
                for ( AnimCurveKey& key : curve._listKey )
                {
                    key._time  = reader.readFloat();
                    key._value = reader.readFloat();
                }
                _listCurve.push_back( std::move( curve ) );
            }
        }

        const bool bCodecKnown  = codecValue < static_cast<uint32>( AnimCodecId::Count );
        const bool bRootInRange = _rootMotionTrack >= -1 && _rootMotionTrack < static_cast<int32>( _listTrackName.size() );
        const bool bValidHeader = MathUtil::isFinite( _duration ) && _duration >= 0.0f;
        if ( reader._bFailed || reader._offset != byteCount || bCodecKnown == false || bRootInRange == false || bValidHeader == false )
        {
            SW_LOG_ERROR( "Animation clip '%#' is malformed", sourceLabel );
            clear();
            return false;
        }
        _codecId = static_cast<AnimCodecId>( codecValue );
        return true;
    }

    bool AnimClip::saveToFile( string_view path ) const
    {
        vector<uint8> bytes;
        makeBytes( bytes );
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeFile( path, bytes.data(), bytes.size() );
    }

    bool AnimClip::loadFromResource( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        vector<uint8> bytes;
        if ( ResourceUtil::readBinaryResource( path, bytes ) == false )
        {
            SW_LOG_ERROR( "Animation clip '%#' could not be read", path );
            clear();
            return false;
        }
        return readFromBytes( bytes.data(), bytes.size(), path );
    }
} // namespace sw
