#include "pch.h"

#include "GameFramework/Kits/Casual/KartRacing/KartTrack.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "KartTrack" );

    namespace
    {
        struct KartTrackInternal
        {
            static constexpr float32 kMinSegmentLength = 1.0e-4f;

            /** @brief 수평 길이입니다. */
            static float32 computeFlatLength( float32 x, float32 z ) { return MathUtil::sqrt( x * x + z * z ); }

            /** @brief 수평 단위 접선입니다(길이가 0 이면 +Z). */
            static float3 makeFlatDirection( const float3& from, const float3& to )
            {
                const float32 deltaX = to._x - from._x;
                const float32 deltaZ = to._z - from._z;
                const float32 length = computeFlatLength( deltaX, deltaZ );
                if ( length < kMinSegmentLength )
                    return float3{ 0.0f, 0.0f, 1.0f };
                return float3{ deltaX / length, 0.0f, deltaZ / length };
            }

            /** @brief 감긴 번호의 조절점입니다(닫힌 곡선). */
            static const float3& getWrapped( const vector<float3>& listPoint, int32 index )
            {
                const int32 count = static_cast<int32>( listPoint.size() );
                return listPoint[static_cast<size_t>( ( index % count + count ) % count )];
            }

            static float3 makeRight( const float3& tangent ) { return float3{ tangent._z, 0.0f, -tangent._x }; }

            /** @brief 수평 외적(z 성분 부호)입니다 — 선분 교차에 씁니다. */
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

        // 1) 닫힌 Catmull-Rom — 조절점 i 와 i+1 사이를 i−1, i+2 를 이웃으로 보간한다.
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

        // 2) 누적 거리 표(수평 길이 — 차는 XZ 로 달린다).
        const size_t count = _listSample.size();
        for ( size_t sampleIndex = 0; sampleIndex < count; ++sampleIndex )
        {
            _listSample[sampleIndex]._distance = _length;
            const float3& from                 = _listSample[sampleIndex]._position;
            const float3& to                   = _listSample[( sampleIndex + 1 ) % count]._position;
            _length += KartTrackInternal::computeFlatLength( to._x - from._x, to._z - from._z );
        }

        // 3) 문 — 결승선(0) 다음 체크포인트를 거리 순으로.
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

        // 4) 아이템 상자 · 부스트 패드를 거리로 푼다.
        for ( const KartItemBoxDef& box : def._listItemBox )
            _listItemBoxPosition.push_back( makeSurfacePoint( box._at, box._offset ) );
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
        float32     bestDistanceSq = MathUtil::MaxFloat;
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
        if ( isValid() == false || searchRange <= 0.0f || searchRange * 2.0f >= _length )
            return project( position );
        // 힌트 거리의 구간에서 앞뒤로 범위만큼 — 샘플 간격이 고르지 않아도 거리로 센다.
        const int32   count  = static_cast<int32>( _listSample.size() );
        const float32 hint   = wrapDistance( hintDistance );
        int32         center = 0;
        for ( int32 segment = 0; segment < count; ++segment )
        {
            if ( _listSample[static_cast<size_t>( segment )]._distance <= hint )
                center = segment;
        }
        KartTrackProjection best           = projectSegment( position, center );
        float32             bestDistanceSq = MathUtil::MaxFloat;
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
        // 이진 탐색 — 누적 거리 표는 오름차순이다.
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
        if ( change > MathUtil::Pi )
            change -= 2.0f * MathUtil::Pi;
        else if ( change < -MathUtil::Pi )
            change += 2.0f * MathUtil::Pi;
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
        // from + move·s = left + gate·u, s ∈ (0, 1], u ∈ [0, 1] — 끝점에서 시작한 이동은 이미 지난 것이라 0 은 뺀다.
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
    bool KartTrackCatalog::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &KartTrackCatalog::loadRoot, path, "KartTrackCatalog" );
    }

    bool KartTrackCatalog::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &KartTrackCatalog::loadRoot, xmlText, sourceName, "KartTrackCatalog" );
    }

    uint32 KartTrackCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode trackNode = root.findChild( "Track" ); trackNode; trackNode = trackNode.findNextSibling( "Track" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( trackNode, sourceName );
            if ( pId == nullptr )
                continue;
            KartTrackDef def;
            def._id                = hashed_string( pId );
            const utf8* pName      = trackNode.findAttribute( "name" );
            def._name              = pName != nullptr ? pName : pId;
            def._width             = trackNode.getAttributeFloat( "width", def._width );
            def._offroadScale      = trackNode.getAttributeFloat( "offroadScale", def._offroadScale );
            def._gateMargin        = trackNode.getAttributeFloat( "gateMargin", def._gateMargin );
            def._lapCount          = MathUtil::max( 1, trackNode.getAttributeInt( "laps", def._lapCount ) );
            def._samplesPerSegment = MathUtil::max( 1, trackNode.getAttributeInt( "samples", def._samplesPerSegment ) );

            for ( XmlNode pointNode = trackNode.findChild( "Point" ); pointNode; pointNode = pointNode.findNextSibling( "Point" ) )
            {
                def._listControlPoint.push_back(
                    float3{ pointNode.getAttributeFloat( "x", 0.0f ), pointNode.getAttributeFloat( "y", 0.0f ), pointNode.getAttributeFloat( "z", 0.0f ) } );
            }
            for ( XmlNode node = trackNode.findChild( "Checkpoint" ); node; node = node.findNextSibling( "Checkpoint" ) )
                def._listCheckpoint.push_back( node.getAttributeFloat( "at", 0.0f ) );
            for ( XmlNode node = trackNode.findChild( "ItemBox" ); node; node = node.findNextSibling( "ItemBox" ) )
            {
                KartItemBoxDef box;
                box._at     = node.getAttributeFloat( "at", 0.0f );
                box._offset = node.getAttributeFloat( "offset", 0.0f );
                def._listItemBox.push_back( box );
            }
            for ( XmlNode node = trackNode.findChild( "BoostPad" ); node; node = node.findNextSibling( "BoostPad" ) )
            {
                KartBoostPadDef pad;
                pad._at       = node.getAttributeFloat( "at", 0.0f );
                pad._offset   = node.getAttributeFloat( "offset", 0.0f );
                pad._length   = node.getAttributeFloat( "length", pad._length );
                pad._width    = node.getAttributeFloat( "width", pad._width );
                pad._duration = node.getAttributeFloat( "duration", pad._duration );
                def._listBoostPad.push_back( pad );
            }
            for ( XmlNode node = trackNode.findChild( "Offroad" ); node; node = node.findNextSibling( "Offroad" ) )
            {
                KartOffroadZoneDef zone;
                zone._from       = node.getAttributeFloat( "from", 0.0f );
                zone._to         = node.getAttributeFloat( "to", 0.0f );
                zone._minOffset  = node.getAttributeFloat( "minOffset", zone._minOffset );
                zone._maxOffset  = node.getAttributeFloat( "maxOffset", zone._maxOffset );
                zone._speedScale = node.getAttributeFloat( "scale", zone._speedScale );
                def._listOffroadZone.push_back( zone );
            }
            if ( def._listControlPoint.size() < 3 )
            {
                SW_LOG_WARNING( "%#: track '%#' has fewer than 3 points - skipped", sourceName, pId );
                continue;
            }
            addTrack( def );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Track> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
