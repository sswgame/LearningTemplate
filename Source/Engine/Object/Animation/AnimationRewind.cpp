#include "pch.h"

#include "Engine/Object/Animation/AnimationRewind.h"

#if SW_ANIMATION_REWIND_ENABLED

    #include "Core/GlobalVariable/GlobalVariableManager.h"
    #include "Core/Math/MathUtil.h"

    #include "Engine/Animation/Pose.h"
    #include "Engine/Animation/Skeleton.h"
    #include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
    #include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    /**
     * @brief `-gv_animationRewind=1` — 애니메이션 되감기 기록을 켭니다(기본 꺼짐). 콘솔 `anim.rewind on|off` · 에디터 Animation Rewind 패널도 이것을 바꿉니다.
     */
    SW_TEST_GLOBAL_VARIABLE_INT( gv_animationRewind, 0, "Record animation rewind history (poses, graph state, notifies, curves, root motion)" );
    /** @brief `-gv_animationRewindSeconds=<초>` — 되감기 기록이 남기는 시간입니다. */
    SW_TEST_GLOBAL_VARIABLE_FLOAT( gv_animationRewindSeconds, 10.0f, "Seconds of animation rewind history kept per unit" );

    namespace
    {
        struct AnimationRewindInternal
        {
            /** @brief 본 하나가 차지하는 바이트입니다(회전 넷 + 이동 셋, 스케일을 실으면 셋 더 — 모두 int16). */
            static constexpr uint32 kRotationTranslationBytes = 7 * sizeof( int16 );
            static constexpr uint32 kScaleBytes               = 3 * sizeof( int16 );
            /** @brief 스케일이 1 에서 이만큼 벗어나야 싣습니다. */
            static constexpr float32 kScaleEpsilon = 1.0e-4f;
            /** @brief 프레임 머리 · 상태의 대략 바이트(진단 표시용)입니다. */
            static constexpr uint64 kFrameOverheadBytes = sizeof( AnimationRewindFrame );

            static int16 quantize( float32 value, float32 range )
            {
                if ( range <= 0.0f )
                    return 0;
                const float32 normalized = MathUtil::clamp( value / range, -1.0f, 1.0f );
                return static_cast<int16>( MathUtil::round( normalized * 32767.0f ) );
            }

            static float32 dequantize( int16 value, float32 range ) { return static_cast<float32>( value ) / 32767.0f * range; }

            static void appendInt16( vector<uint8>& inoutListByte, int16 value )
            {
                const uint16 bits = static_cast<uint16>( value );
                inoutListByte.push_back( static_cast<uint8>( bits & 0xFFu ) );
                inoutListByte.push_back( static_cast<uint8>( bits >> 8 ) );
            }

            static int16 readInt16( const uint8* pByte ) { return static_cast<int16>( static_cast<uint16>( pByte[0] | ( pByte[1] << 8 ) ) ); }
        };
    } // namespace

    const AnimationRewindFrame* AnimationRewindTrack::findFrame( float64 time ) const
    {
        if ( _count == 0 )
            return nullptr;
        // 시간 순 고리 — 이분 탐색으로 time 이하의 마지막을 찾는다.
        uint32 low  = 0;
        uint32 high = _count;
        while ( low < high )
        {
            const uint32 middle = ( low + high ) / 2;
            if ( getFrame( middle )._time <= time )
                low = middle + 1;
            else
                high = middle;
        }
        return &getFrame( low > 0 ? low - 1 : 0 );
    }

    AnimationRewindRecorder::AnimationRewindRecorder()
        : _listTrack{}
        , _clock{ 0.0 }
        , _scrubTime{ 0.0 }
        , _windowSeconds{ 10.0f }
        , _bEnabled{ SW_FALSE }
        , _bScrubbing{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AnimationRewindRecorder::setRecordingRequested( bool bRecord )
    {
        gv_animationRewind = bRecord ? 1 : 0;
    }

    bool AnimationRewindRecorder::isRecordingRequested()
    {
        return gv_animationRewind != 0;
    }

    void AnimationRewindRecorder::setRequestedWindowSeconds( float32 seconds )
    {
        gv_animationRewindSeconds = MathUtil::max( seconds, 0.1f );
    }

    float32 AnimationRewindRecorder::getRequestedWindowSeconds()
    {
        return gv_animationRewindSeconds;
    }

    void AnimationRewindRecorder::syncWithRequest()
    {
        const bool bRequested = isRecordingRequested();
        if ( bRequested != isEnabled() )
            setEnabled( bRequested );
        const float32 requestedSeconds = MathUtil::max( static_cast<float32>( gv_animationRewindSeconds ), 0.1f );
        if ( requestedSeconds != _windowSeconds )
            setWindowSeconds( requestedSeconds );
    }

    void AnimationRewindRecorder::endFrame( float32 deltaSeconds )
    {
        if ( _bEnabled == SW_FALSE )
            return;
        trimToWindow();
        _clock += static_cast<float64>( deltaSeconds );
    }

    void AnimationRewindRecorder::setEnabled( bool bEnabled )
    {
        _bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bEnabled == false )
            clear();
    }

    void AnimationRewindRecorder::setWindowSeconds( float32 seconds )
    {
        _windowSeconds = MathUtil::max( seconds, 0.1f );
        trimToWindow();
    }

    AnimationRewindTrack& AnimationRewindRecorder::acquireTrack( const Component& target, AnimationRewindKind kind )
    {
        const ComponentHandle handle = target.getHandle();
        for ( AnimationRewindTrack& track : _listTrack )
        {
            if ( track._target == handle )
                return track;
        }
        AnimationRewindTrack track{};
        track._target = handle;
        track._kind   = kind;
        track._label  = ( target.getOwner() != nullptr ) ? string( target.getOwner()->getName().c_str() ) : string( "(no owner)" );
        _listTrack.push_back( std::move( track ) );
        return _listTrack.back();
    }

    AnimationRewindFrame& AnimationRewindRecorder::appendFrame( AnimationRewindTrack& track )
    {
        // 가장 오래된 프레임이 창 밖이면 그 자리를 다시 쓴다(용량 · 목록의 할당을 그대로 쓴다). 아니면 고리를 늘린다.
        if ( track._count > 0 && _clock - track.getFrame( 0 )._time > static_cast<float64>( _windowSeconds ) )
        {
            AnimationRewindFrame& reused = track._listFrame[track._head];
            track._head                  = ( track._head + 1 ) % static_cast<uint32>( track._listFrame.size() );
            return reused;
        }
        if ( track._count < track._listFrame.size() )
        {
            ++track._count;
            return track._listFrame[( track._head + track._count - 1 ) % track._listFrame.size()];
        }
        // 꽉 찼다 — 순서대로 펴고(머리 = 0) 뒤에 붙인다.
        std::rotate( track._listFrame.begin(), track._listFrame.begin() + track._head, track._listFrame.end() );
        track._head = 0;
        track._listFrame.emplace_back();
        ++track._count;
        return track._listFrame.back();
    }

    void AnimationRewindRecorder::recordUnit( const SkeletalMeshComponent& unit, uint64 frameIndex )
    {
        if ( _bEnabled == SW_FALSE || _bScrubbing == SW_TRUE )
            return;
        AnimationRewindTrack& track = acquireTrack( unit, AnimationRewindKind::Skeletal );
        if ( track._listParentIndex.size() != unit.getSkeleton().getBoneCount() )
            track._listParentIndex = unit.getSkeleton().getParentIndices();
        AnimationRewindFrame& frame = appendFrame( track );
        frame._time                 = _clock;
        frame._frameIndex           = frameIndex;
        frame._worldMatrix          = unit.getWorldMatrix();
        encodePose( unit.getLocalPose(), frame );
        frame._state.reset();
        unit.collectDebugState( frame._state );
    }

    void AnimationRewindRecorder::recordState( const Component& target, AnimationRewindKind kind, const AnimationDebugState& state, uint64 frameIndex )
    {
        if ( _bEnabled == SW_FALSE || _bScrubbing == SW_TRUE )
            return;
        AnimationRewindTrack& track = acquireTrack( target, kind );
        AnimationRewindFrame& frame = appendFrame( track );
        frame._time                 = _clock;
        frame._frameIndex           = frameIndex;
        frame._worldMatrix          = float4x4::Identity;
        frame._poseByte.clear();
        frame._boneCount = 0;
        frame._state     = state;
    }

    void AnimationRewindRecorder::trimToWindow()
    {
        const float64 oldest = _clock - static_cast<float64>( _windowSeconds );
        for ( AnimationRewindTrack& track : _listTrack )
        {
            while ( track._count > 0 && track.getFrame( 0 )._time < oldest )
            {
                track._head = ( track._head + 1 ) % static_cast<uint32>( track._listFrame.size() );
                --track._count;
            }
        }
        _listTrack.erase( std::remove_if( _listTrack.begin(), _listTrack.end(), []( const AnimationRewindTrack& track )
        { return track._count == 0; } ),
                          _listTrack.end() );
    }

    void AnimationRewindRecorder::setScrubTime( float64 time )
    {
        _scrubTime  = MathUtil::clamp( time, getEarliestTime(), getLatestTime() );
        _bScrubbing = SW_TRUE;
    }

    float64 AnimationRewindRecorder::getEarliestTime() const
    {
        float64 earliest = _clock;
        for ( const AnimationRewindTrack& track : _listTrack )
            earliest = MathUtil::min( earliest, track.getFrame( 0 )._time );
        return earliest;
    }

    float64 AnimationRewindRecorder::getLatestTime() const
    {
        float64 latest    = 0.0;
        bool    bAnyFrame = false;
        for ( const AnimationRewindTrack& track : _listTrack )
        {
            latest    = bAnyFrame ? MathUtil::max( latest, track.getFrame( track._count - 1 )._time ) : track.getFrame( track._count - 1 )._time;
            bAnyFrame = true;
        }
        return bAnyFrame ? latest : _clock;
    }

    const AnimationRewindTrack* AnimationRewindRecorder::findTrack( const ComponentHandle& target ) const
    {
        for ( const AnimationRewindTrack& track : _listTrack )
        {
            if ( track._target == target )
                return &track;
        }
        return nullptr;
    }

    uint64 AnimationRewindRecorder::getByteCount() const
    {
        uint64 byteCount = 0;
        for ( const AnimationRewindTrack& track : _listTrack )
        {
            for ( uint32 order = 0; order < track._count; ++order )
            {
                const AnimationRewindFrame& frame = track.getFrame( order );
                byteCount += AnimationRewindInternal::kFrameOverheadBytes + frame._poseByte.size() +
                             ( frame._state._listNotify.size() + frame._state._listCurveName.size() ) * sizeof( hashed_string ) +
                             frame._state._listCurveValue.size() * sizeof( float32 );
            }
        }
        return byteCount;
    }

    void AnimationRewindRecorder::clear()
    {
        _listTrack.clear();
        _bScrubbing = SW_FALSE;
    }

    void AnimationRewindRecorder::encodePose( const Pose& pose, AnimationRewindFrame& outFrame )
    {
        const uint32 boneCount = pose.getBoneCount();
        float32      translationRange{ 0.0f };
        float32      scaleRange{ 0.0f };
        bool         bScaled = false;
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const float3& translation = pose.getTranslations()[boneIndex];
            const float3& scale       = pose.getScales()[boneIndex];
            translationRange          = MathUtil::max( translationRange, MathUtil::max( MathUtil::abs( translation._x ), MathUtil::max( MathUtil::abs( translation._y ), MathUtil::abs( translation._z ) ) ) );
            scaleRange                = MathUtil::max( scaleRange, MathUtil::max( MathUtil::abs( scale._x ), MathUtil::max( MathUtil::abs( scale._y ), MathUtil::abs( scale._z ) ) ) );
            bScaled                   = bScaled || MathUtil::abs( scale._x - 1.0f ) > AnimationRewindInternal::kScaleEpsilon || MathUtil::abs( scale._y - 1.0f ) > AnimationRewindInternal::kScaleEpsilon ||
                      MathUtil::abs( scale._z - 1.0f ) > AnimationRewindInternal::kScaleEpsilon;
        }
        outFrame._boneCount        = boneCount;
        outFrame._translationRange = translationRange;
        outFrame._scaleRange       = bScaled ? scaleRange : 0.0f;
        outFrame._poseByte.clear();
        outFrame._poseByte.reserve( static_cast<size_t>( boneCount ) *
                                    ( AnimationRewindInternal::kRotationTranslationBytes + ( bScaled ? AnimationRewindInternal::kScaleBytes : 0u ) ) );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            // 회전은 w ≥ 0 쪽으로 맞춰(같은 회전) 성분마다 [-1, 1] 을 int16 으로.
            quaternion rotation = pose.getRotations()[boneIndex];
            rotation.normalize();
            const float32 sign           = rotation._w < 0.0f ? -1.0f : 1.0f;
            const float32 arrRotation[4] = { rotation._x * sign, rotation._y * sign, rotation._z * sign, rotation._w * sign };
            for ( const float32 component : arrRotation )
                AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( component, 1.0f ) );
            const float3& translation = pose.getTranslations()[boneIndex];
            AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( translation._x, translationRange ) );
            AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( translation._y, translationRange ) );
            AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( translation._z, translationRange ) );
            if ( bScaled == false )
                continue;
            const float3& scale = pose.getScales()[boneIndex];
            AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( scale._x, scaleRange ) );
            AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( scale._y, scaleRange ) );
            AnimationRewindInternal::appendInt16( outFrame._poseByte, AnimationRewindInternal::quantize( scale._z, scaleRange ) );
        }
    }

    void AnimationRewindRecorder::decodePose( const AnimationRewindFrame& frame, Pose& outPose )
    {
        outPose.resize( frame._boneCount );
        const bool   bScaled   = frame._scaleRange > 0.0f;
        const uint32 boneBytes = AnimationRewindInternal::kRotationTranslationBytes + ( bScaled ? AnimationRewindInternal::kScaleBytes : 0u );
        if ( frame._poseByte.size() < static_cast<size_t>( boneBytes ) * frame._boneCount )
            return;
        const uint8* pByte = frame._poseByte.data();
        for ( uint32 boneIndex = 0; boneIndex < frame._boneCount; ++boneIndex, pByte += boneBytes )
        {
            BoneTransform transform{};
            transform._rotation = quaternion{ AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte ), 1.0f ),
                                              AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 2 ), 1.0f ),
                                              AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 4 ), 1.0f ),
                                              AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 6 ), 1.0f ) }
                                      .normalize();
            transform._translation = float3{ AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 8 ), frame._translationRange ),
                                             AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 10 ), frame._translationRange ),
                                             AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 12 ), frame._translationRange ) };
            if ( bScaled )
                transform._scale = float3{ AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 14 ), frame._scaleRange ),
                                           AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 16 ), frame._scaleRange ),
                                           AnimationRewindInternal::dequantize( AnimationRewindInternal::readInt16( pByte + 18 ), frame._scaleRange ) };
            outPose.setBoneTransform( boneIndex, transform );
        }
    }

    void AnimationRewindRecorder::computeWorldBonePositions( const AnimationRewindTrack& track, const AnimationRewindFrame& frame, vector<float3>& outListPosition )
    {
        outListPosition.clear();
        if ( frame._boneCount == 0 || track._listParentIndex.size() != frame._boneCount )
            return;
        Pose pose;
        decodePose( frame, pose );
        vector<float4x4> listModel;
        pose.computeModelSpace( track._listParentIndex, listModel );
        for ( const float4x4& model : listModel )
            outListPosition.push_back( float3::transform( model.getTranslation(), frame._worldMatrix ) );
    }
} // namespace sw

#endif
