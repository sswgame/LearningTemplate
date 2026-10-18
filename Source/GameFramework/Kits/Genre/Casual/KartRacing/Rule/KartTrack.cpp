#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartTrack.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "KartTrack" );

    namespace
    {
        struct KartTrackInternal
        {
            static constexpr float32 kMinSegmentLength = 1.0e-4f;

            /** @brief 수평 길이를 반환합니다. */
            static float32 computeFlatLength( float32 x, float32 z ) { return MathUtil::sqrt( x * x + z * z ); }

            /** @brief 수평 단위 접선을 반환합니다(길이가 0 이면 +Z). */
            static float3 makeFlatDirection( const float3& from, const float3& to )
            {
                const float32 deltaX = to._x - from._x;
                const float32 deltaZ = to._z - from._z;
                const float32 length = computeFlatLength( deltaX, deltaZ );
                if ( length < kMinSegmentLength )
                    return float3{ 0.0f, 0.0f, 1.0f };
                return float3{ deltaX / length, 0.0f, deltaZ / length };
            }

            /** @brief 감긴 번호의 조절점을 반환합니다(닫힌 곡선). */
            static const float3& getWrapped( const vector<float3>& listPoint, int32 index )
            {
                const int32 count = static_cast<int32>( listPoint.size() );
                return listPoint[static_cast<size_t>( ( index % count + count ) % count )];
            }

            static float3 makeRight( const float3& tangent ) { return float3{ tangent._z, 0.0f, -tangent._x }; }

            /** @brief 수평 외적(z 성분의 부호)을 반환합니다. 선분 교차 검사에 씁니다. */
            static float32 computeFlatCross( float32 ax, float32 az, float32 bx, float32 bz ) { return ax * bz - az * bx; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    KartTrack::KartTrack()
        : _def{}
        , _listSample{}
        , _listGate{}
        , _listItemBoxPosition{}
        , _listBoostPad{}
        , _length{ 0.0f }
    {
    }

    bool KartTrack::initialize( const KartTrackDef& def )
    {
        _listSample.clear();
        _listGate.clear();
        _listItemBoxPosition.clear();
        _listBoostPad.clear();
        _length = 0.0f;
        _def    = def;
        if ( def._listControlPoint.size() < 3 || def._width <= 0.0f )
        {
            SW_LOG_WARNING( "track '%#' needs at least 3 control points and a positive width", def._id.c_str() );
            return false;
        }

        // 1) 닫힌 Catmull-Rom: 조절점 i 와 i+1 사이를 i-1, i+2 를 이웃으로 보간한다.
        const vector<float3>& listPoint   = def._listControlPoint;
        const int32           pointCount  = static_cast<int32>( listPoint.size() );
        const int32           sampleCount = MathUtil::max( 1, def._samplesPerSegment );
        for ( int32 pointIndex = 0; pointIndex < pointCount; ++pointIndex )
        {
            for ( int32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
            {
                const float32 t = static_cast<float32>( sampleIndex ) / static_cast<float32>( sampleCount );
                Sample        sampleValue;
                sampleValue._position = float3::catmullRom( KartTrackInternal::getWrapped( listPoint, pointIndex - 1 ), KartTrackInternal::getWrapped( listPoint, pointIndex ),
                                                            KartTrackInternal::getWrapped( listPoint, pointIndex + 1 ),
                                                            KartTrackInternal::getWrapped( listPoint, pointIndex + 2 ), t );
                _listSample.push_back( sampleValue );
            }
        }

        // 2) 누적 거리 테이블(수평 길이이며, 차는 XZ 평면에서 달린다).
        const size_t count = _listSample.size();
        for ( size_t sampleIndex = 0; sampleIndex < count; ++sampleIndex )
        {
            _listSample[sampleIndex]._distance = _length;
            const float3& from                 = _listSample[sampleIndex]._position;
            const float3& to                   = _listSample[( sampleIndex + 1 ) % count]._position;
            _length += KartTrackInternal::computeFlatLength( to._x - from._x, to._z - from._z );
        }

        // 3) 문: 결승선(0) 다음에 체크포인트를 거리 순으로 둔다.
        vector<float32> listCheckpoint = def._listCheckpoint;
        std::sort( listCheckpoint.begin(), listCheckpoint.end() );
        _def._listCheckpoint.clear();
        _listGate.push_back( makeGate( 0.0f ) );
        for ( const float32 fraction : listCheckpoint )
        {
            if ( fraction <= 0.0f || fraction >= 1.0f )
            {
                SW_LOG_WARNING( "track '%#': checkpoint at %# is outside (0, 1) - skipped", def._id.c_str(), fraction );
                continue;
            }
            _def._listCheckpoint.push_back( fraction );
            _listGate.push_back( makeGate( fraction * _length ) );
        }

        // 4) 아이템 상자와 부스트 패드를 거리로 바꾼다.
        for ( const KartItemBoxDef& box : def._listItemBox )
        {
            _listItemBoxPosition.push_back( makeSurfacePoint( box._at, box._offset ) );
        }
        for ( const KartBoostPadDef& pad : def._listBoostPad )
        {
            KartBoostPad boostPad;
            boostPad._def      = pad;
            boostPad._distance = wrapDistance( pad._at * _length );
            _listBoostPad.push_back( boostPad );
        }
        return true;
    }

    KartTrackProjection KartTrack::projectSegment( const float3& position, int32 segment ) const
    {
        const size_t  count      = _listSample.size();
        const Sample& from       = _listSample[static_cast<size_t>( segment )];
        const Sample& to         = _listSample[( static_cast<size_t>( segment ) + 1 ) % count];
        const float32 deltaX     = to._position._x - from._position._x;
        const float32 deltaZ     = to._position._z - from._position._z;
        const float32 lengthSq   = deltaX * deltaX + deltaZ * deltaZ;
        float32       segmentPos = 0.0f;
        if ( lengthSq > KartTrackInternal::kMinSegmentLength )
            segmentPos = MathUtil::saturate( ( ( position._x - from._position._x ) * deltaX + ( position._z - from._position._z ) * deltaZ ) / lengthSq );

        KartTrackProjection result;
        result._segment             = segment;
        result._position            = float3::lerp( from._position, to._position, segmentPos );
        result._tangent             = KartTrackInternal::makeFlatDirection( from._position, to._position );
        const float32 segmentLength = MathUtil::sqrt( lengthSq );
        result._distance            = wrapDistance( from._distance + segmentLength * segmentPos );
        const float3 right          = KartTrackInternal::makeRight( result._tangent );
        result._offset              = ( position._x - result._position._x ) * right._x + ( position._z - result._position._z ) * right._z;
        return result;
    }

    KartTrackProjection KartTrack::project( const float3& position ) const
    {
        KartTrackProjection best;
        if ( isValid() == false )
            return best;
        float32     bestDistanceSq = MathUtil::kMaxFloat;
        const int32 count          = static_cast<int32>( _listSample.size() );
        for ( int32 segment = 0; segment < count; ++segment )
        {
            const KartTrackProjection candidate = projectSegment( position, segment );
            const float32             deltaX    = position._x - candidate._position._x;
            const float32             deltaZ    = position._z - candidate._position._z;
            const float32             distSq    = deltaX * deltaX + deltaZ * deltaZ;
            if ( distSq < bestDistanceSq )
            {
                bestDistanceSq = distSq;
                best           = candidate;
            }
        }
        return best;
    }

    KartTrackProjection KartTrack::project( const float3& position, float32 hintDistance, float32 searchRange ) const
    {
        const bool bUseGlobalProjection = isValid() == false || searchRange <= 0.0f || searchRange * 2.0f >= _length;
        if ( bUseGlobalProjection )
            return project( position );
        // 힌트 거리의 구간에서 앞뒤로 범위만큼 본다. 샘플 간격이 고르지 않아도 거리로 센다.
        const int32   count  = static_cast<int32>( _listSample.size() );
        const float32 hint   = wrapDistance( hintDistance );
        int32         center = 0;
        for ( int32 segment = 0; segment < count; ++segment )
        {
            if ( _listSample[static_cast<size_t>( segment )]._distance <= hint )
                center = segment;
        }
        KartTrackProjection best           = projectSegment( position, center );
        float32             bestDistanceSq = MathUtil::kMaxFloat;
        for ( int32 step = -count; step <= count; ++step )
        {
            const int32   segment = ( ( center + step ) % count + count ) % count;
            const float32 gap     = MathUtil::abs( computeSignedGap( hint, _listSample[static_cast<size_t>( segment )]._distance ) );
            if ( gap > searchRange )
                continue;
            const KartTrackProjection candidate = projectSegment( position, segment );
            const float32             deltaX    = position._x - candidate._position._x;
            const float32             deltaZ    = position._z - candidate._position._z;
            const float32             distSq    = deltaX * deltaX + deltaZ * deltaZ;
            if ( distSq < bestDistanceSq )
            {
                bestDistanceSq = distSq;
                best           = candidate;
            }
        }
        return best;
    }

    KartTrackFrame KartTrack::sample( float32 distance ) const
    {
        KartTrackFrame frame;
        if ( isValid() == false )
            return frame;
        const float32 wrapped = wrapDistance( distance );
        const size_t  count   = _listSample.size();
        // 이진 탐색이다. 누적 거리 테이블은 오름차순이다.
        size_t low  = 0;
        size_t high = count - 1;
        while ( low < high )
        {
            const size_t middle = ( low + high + 1 ) / 2;
            if ( _listSample[middle]._distance <= wrapped )
                low = middle;
            else
                high = middle - 1;
        }
        const Sample& from          = _listSample[low];
        const Sample& to            = _listSample[( low + 1 ) % count];
        const float32 segmentLength = ( low + 1 == count ? _length : to._distance ) - from._distance;
        const float32 segmentPos    = segmentLength > KartTrackInternal::kMinSegmentLength ? ( wrapped - from._distance ) / segmentLength : 0.0f;
        frame._position             = float3::lerp( from._position, to._position, MathUtil::saturate( segmentPos ) );
        frame._tangent              = KartTrackInternal::makeFlatDirection( from._position, to._position );
        frame._right                = KartTrackInternal::makeRight( frame._tangent );
        return frame;
    }

    float32 KartTrack::wrapDistance( float32 distance ) const
    {
        if ( _length <= 0.0f )
            return 0.0f;
        float32 wrapped = MathUtil::fmod( distance, _length );
        if ( wrapped < 0.0f )
            wrapped += _length;
        if ( wrapped >= _length )
            wrapped = 0.0f;
        return wrapped;
    }

    float32 KartTrack::computeSignedGap( float32 fromDistance, float32 toDistance ) const
    {
        float32 gap = wrapDistance( toDistance - fromDistance );
        if ( gap > _length * 0.5f )
            gap -= _length;
        return gap;
    }

    float32 KartTrack::computeHeadingChange( float32 distance, float32 ahead ) const
    {
        const float3  fromTangent = sample( distance )._tangent;
        const float3  toTangent   = sample( distance + ahead )._tangent;
        const float32 fromYaw     = MathUtil::atan2( fromTangent._x, fromTangent._z );
        float32       change      = MathUtil::atan2( toTangent._x, toTangent._z ) - fromYaw;
        if ( change > MathUtil::kPi )
            change -= 2.0f * MathUtil::kPi;
        else if ( change < -MathUtil::kPi )
            change += 2.0f * MathUtil::kPi;
        return change;
    }

    int32 KartTrack::computeGateCrossing( int32 gateIndex, const float3& from, const float3& to ) const
    {
        if ( gateIndex < 0 || gateIndex >= static_cast<int32>( _listGate.size() ) )
            return 0;
        const KartTrackGate& gate  = _listGate[static_cast<size_t>( gateIndex )];
        const float32        moveX = to._x - from._x;
        const float32        moveZ = to._z - from._z;
        const float32        gateX = gate._right._x - gate._left._x;
        const float32        gateZ = gate._right._z - gate._left._z;
        const float32        denom = KartTrackInternal::computeFlatCross( moveX, moveZ, gateX, gateZ );
        if ( MathUtil::abs( denom ) < KartTrackInternal::kMinSegmentLength )
            return 0;
        // from + move*s = left + gate*u, s ∈ (0, 1], u ∈ [0, 1] 이다. 엔드포인트에서 시작한 이동은 이미 지난 것이므로 0 은 뺀다.
        const float32 offsetX = gate._left._x - from._x;
        const float32 offsetZ = gate._left._z - from._z;
        const float32 moveT   = KartTrackInternal::computeFlatCross( offsetX, offsetZ, gateX, gateZ ) / denom;
        const float32 gateT   = KartTrackInternal::computeFlatCross( offsetX, offsetZ, moveX, moveZ ) / denom;
        const bool    bHit    = 0.0f < moveT && moveT <= 1.0f && 0.0f <= gateT && gateT <= 1.0f;
        if ( bHit == false )
            return 0;
        return moveX * gate._forward._x + moveZ * gate._forward._z >= 0.0f ? 1 : -1;
    }

    int32 KartTrack::findBoostPadAt( const float3& position ) const
    {
        if ( _listBoostPad.empty() )
            return -1;
        const KartTrackProjection projection = project( position );
        for ( size_t padIndex = 0; padIndex < _listBoostPad.size(); ++padIndex )
        {
            const KartBoostPad& pad   = _listBoostPad[padIndex];
            const float32       along = MathUtil::abs( computeSignedGap( pad._distance, projection._distance ) );
            const float32       side  = MathUtil::abs( projection._offset - pad._def._offset );
            if ( along <= pad._def._length * 0.5f && side <= pad._def._width * 0.5f )
                return static_cast<int32>( padIndex );
        }
        return -1;
    }

    bool KartTrack::applyBoostPad( ArcadeVehicleMotor& motor, int32& inoutLastPad ) const
    {
        const int32 pad  = motor.isAirborne() ? -1 : findBoostPadAt( motor.getPosition() );
        const bool  bNew = pad >= 0 && pad != inoutLastPad;
        if ( motor.isAirborne() == false )
            inoutLastPad = pad;
        if ( bNew == false )
            return false;
        motor.startBoost( _listBoostPad[static_cast<size_t>( pad )]._def._duration, -1 );
        return true;
    }

    bool KartTrack::isOnRoad( const float3& position ) const
    {
        return MathUtil::abs( project( position )._offset ) <= _def._width * 0.5f;
    }

    float32 KartTrack::sampleHeight( float32 x, float32 z ) const
    {
        return project( float3{ x, 0.0f, z } )._position._y;
    }

    float32 KartTrack::sampleSpeedScale( float32 x, float32 z ) const
    {
        if ( isValid() == false )
            return 1.0f;
        const KartTrackProjection projection = project( float3{ x, 0.0f, z } );
        float32                   scale      = 1.0f;
        if ( MathUtil::abs( projection._offset ) > _def._width * 0.5f )
            scale = _def._offroadScale;
        for ( const KartOffroadZoneDef& zone : _def._listOffroadZone )
        {
            const bool bAlong = isWithinSpan( projection._distance, zone._from * _length, zone._to * _length );
            const bool bSide  = zone._minOffset <= projection._offset && projection._offset <= zone._maxOffset;
            if ( bAlong && bSide )
                scale = MathUtil::min( scale, zone._speedScale );
        }
        return scale;
    }

    KartTrackGate KartTrack::makeGate( float32 distance ) const
    {
        const KartTrackFrame frame     = sample( distance );
        const float32        halfWidth = _def._width * 0.5f + MathUtil::max( 0.0f, _def._gateMargin );
        KartTrackGate        gate;
        gate._distance = wrapDistance( distance );
        gate._forward  = frame._tangent;
        gate._left     = float3{ frame._position._x - frame._right._x * halfWidth, frame._position._y, frame._position._z - frame._right._z * halfWidth };
        gate._right    = float3{ frame._position._x + frame._right._x * halfWidth, frame._position._y, frame._position._z + frame._right._z * halfWidth };
        return gate;
    }

    float3 KartTrack::makeSurfacePoint( float32 fraction, float32 offset ) const
    {
        const KartTrackFrame frame = sample( fraction * _length );
        return float3{ frame._position._x + frame._right._x * offset, frame._position._y, frame._position._z + frame._right._z * offset };
    }

    bool KartTrack::isWithinSpan( float32 distance, float32 fromDistance, float32 toDistance ) const
    {
        const float32 span = wrapDistance( toDistance - fromDistance );
        return wrapDistance( distance - fromDistance ) <= span;
    }

    // ------------------------------------------------------------------------------
    // 카탈로그
    // ------------------------------------------------------------------------------

    uint32 KartTrackCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 트랙을 넣지 않고 오류다(readFields 가 남긴다).
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            if ( StringUtil::equals( node.getName(), "Track", true ) == false )
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, node.getName() );
            else if ( loadTrack( node, sourceName ) )
                ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_ERROR( "%#: no <Track> entries", sourceName );
        return loadedCount;
    }

    bool KartTrackCatalog::loadTrack( const XMLNode& trackNode, string_view sourceName )
    {
        KartTrackDef def;
        bool         bValid = GameDataXML::readFields( trackNode, def, sourceName );
        for ( XMLNode node = trackNode.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( name == "Point" )
            {
                KartTrackPointDef point;
                bValid = GameDataXML::readFields( node, point, sourceName ) && bValid;
                def._listControlPoint.push_back( float3{ point._x, point._y, point._z } );
            }
            else if ( name == "Checkpoint" )
            {
                KartCheckpointDef checkpoint;
                bValid = GameDataXML::readFields( node, checkpoint, sourceName ) && bValid;
                def._listCheckpoint.push_back( checkpoint._at );
            }
            else if ( name == "ItemBox" )
            {
                KartItemBoxDef box;
                bValid = GameDataXML::readFields( node, box, sourceName ) && bValid;
                def._listItemBox.push_back( box );
            }
            else if ( name == "BoostPad" )
            {
                KartBoostPadDef pad;
                bValid = GameDataXML::readFields( node, pad, sourceName ) && bValid;
                def._listBoostPad.push_back( pad );
            }
            else if ( name == "Offroad" )
            {
                KartOffroadZoneDef zone;
                bValid = GameDataXML::readFields( node, zone, sourceName ) && bValid;
                def._listOffroadZone.push_back( zone );
            }
            else
            {
                SW_LOG_ERROR( "%#: track '%#' has an unknown child <%#>", sourceName, def._id.c_str(), name );
                bValid = false;
            }
        }
        if ( def._listControlPoint.size() < 3 )
        {
            SW_LOG_ERROR( "%#: track '%#' needs 3 or more <Point>", sourceName, def._id.c_str() );
            bValid = false;
        }
        if ( bValid )
            add( def );
        return bValid;
    }
} // namespace sw
