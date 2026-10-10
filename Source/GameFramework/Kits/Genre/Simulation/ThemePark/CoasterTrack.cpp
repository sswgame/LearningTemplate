#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/CoasterTrack.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/World/Spline/ArcLengthUtil.h"

namespace sw
{
    SW_LOG_CALLER( "CoasterTrack" );

    namespace
    {
        struct CoasterTrackInternal
        {
            static constexpr float32 kDuplicateEpsilon = 1.0e-4f;

            /** @brief 열거자 이름표 한 줄입니다. */
            struct PieceName
            {
                CoasterPieceType _type;
                const utf8*      _pName;
            };

            static constexpr PieceName kArrPieceName[] = {
                {  CoasterPieceType::Station,   "Station"},
                { CoasterPieceType::Straight,  "Straight"},
                { CoasterPieceType::LiftHill,  "LiftHill"},
                {     CoasterPieceType::Drop,      "Drop"},
                {     CoasterPieceType::Hill,      "Hill"},
                { CoasterPieceType::TurnLeft,  "TurnLeft"},
                {CoasterPieceType::TurnRight, "TurnRight"},
                {     CoasterPieceType::Loop,      "Loop"},
                {   CoasterPieceType::Brakes,    "Brakes"},
                {  CoasterPieceType::Booster,   "Booster"},
            };

            static float32 smoothStepSlope( float32 t ) { return 6.0f * t * ( 1.0f - t ); }

            /** @brief 길이가 거의 0 이면 @p fallback 을 돌려주는 정규화입니다. */
            static float3 normalizeOr( const float3& value, const float3& fallback )
            {
                const float32 length = value.getLength();
                if ( length < 1.0e-6f )
                    return fallback;
                return value * ( 1.0f / length );
            }

            /** @brief @p up 을 @p tangent 에 직교하는 단위 벡터로 맞춥니다. 퇴화하면 @p fallback 입니다. */
            static float3 orthogonalize( const float3& up, const float3& tangent, const float3& fallback )
            {
                return normalizeOr( up - tangent * up.dot( tangent ), fallback );
            }

            /** @brief 요 @p heading(라디안)의 수평 앞 · 오른쪽입니다. */
            static float3 forwardOf( float32 heading ) { return float3{ MathUtil::sin( heading ), 0.0f, MathUtil::cos( heading ) }; }
            static float3 rightOf( float32 heading ) { return float3{ MathUtil::cos( heading ), 0.0f, -MathUtil::sin( heading ) }; }

            static uint8 flagsOf( CoasterPieceType type )
            {
                switch ( type )
                {
                    case CoasterPieceType::Station:
                        return CoasterSegmentFlag::kStation;
                    case CoasterPieceType::LiftHill:
                        return CoasterSegmentFlag::kLift;
                    case CoasterPieceType::Brakes:
                        return CoasterSegmentFlag::kBrake;
                    case CoasterPieceType::Booster:
                        return CoasterSegmentFlag::kBooster;
                    case CoasterPieceType::Straight:
                    case CoasterPieceType::Drop:
                    case CoasterPieceType::Hill:
                    case CoasterPieceType::TurnLeft:
                    case CoasterPieceType::TurnRight:
                    case CoasterPieceType::Loop:
                        return CoasterSegmentFlag::kNone;
                }
                return CoasterSegmentFlag::kNone;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseCoasterPieceType( string_view text, CoasterPieceType& outType )
    {
        for ( const CoasterTrackInternal::PieceName& entry : CoasterTrackInternal::kArrPieceName )
        {
            if ( StringUtil::equals( text, string_view( entry._pName ), true ) )
            {
                outType = entry._type;
                return true;
            }
        }
        return false;
    }

    const utf8* toString( CoasterPieceType type )
    {
        for ( const CoasterTrackInternal::PieceName& entry : CoasterTrackInternal::kArrPieceName )
        {
            if ( entry._type == type )
                return entry._pName;
        }
        return "Unknown";
    }

    // ------------------------------------------------------------------------------
    // CoasterTrack
    // ------------------------------------------------------------------------------
    CoasterTrack::CoasterTrack()
        : _listPoint{}
        , _length{ 0.0f }
        , _maxHeight{ 0.0f }
        , _minHeight{ 0.0f }
        , _bClosed{ SW_FALSE }
    {
    }

    void CoasterTrack::clear()
    {
        _listPoint.clear();
        _length    = 0.0f;
        _maxHeight = 0.0f;
        _minHeight = 0.0f;
        _bClosed   = SW_FALSE;
    }

    void CoasterTrack::appendPoint( const float3& position, const float3& up, uint8 flags )
    {
        const bool bDuplicate = _listPoint.empty() == false &&
                                float3::getDistance( _listPoint.back()._position, position ) < CoasterTrackInternal::kDuplicateEpsilon;
        if ( bDuplicate )
            return;
        Point point;
        point._position = position;
        point._up       = up;
        point._flags    = flags;
        _listPoint.push_back( point );
    }

    void CoasterTrack::finalize( bool bClosed )
    {
        _bClosed = bClosed ? SW_TRUE : SW_FALSE;
        // 닫힌 회로에서 마지막 점이 첫 점과 같으면 한 점이다.
        const bool bLastIsFirst = bClosed && _listPoint.size() > 2 &&
                                  float3::getDistance( _listPoint.back()._position, _listPoint.front()._position ) < CoasterTrackInternal::kDuplicateEpsilon * 10.0f;
        if ( bLastIsFirst )
            _listPoint.pop_back();

        const size_t pointCount = _listPoint.size();
        _length                 = 0.0f;
        if ( pointCount < 2 )
            return;

        _maxHeight = _listPoint[0]._position._y;
        _minHeight = _listPoint[0]._position._y;
        for ( size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex )
        {
            Point& point    = _listPoint[pointIndex];
            point._distance = _length;
            if ( pointIndex + 1 < pointCount )
                _length += float3::getDistance( point._position, _listPoint[pointIndex + 1]._position );
            _maxHeight = MathUtil::max( _maxHeight, point._position._y );
            _minHeight = MathUtil::min( _minHeight, point._position._y );
        }
        if ( bClosed )
            _length += float3::getDistance( _listPoint.back()._position, _listPoint.front()._position );

        // 접선은 이웃 점의 중심 차분 — 닫힌 회로는 감아서, 열린 트랙의 끝은 한쪽 차분.
        float3 previousUp{ 0.0f, 1.0f, 0.0f };
        for ( size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex )
        {
            const bool   bFirst    = pointIndex == 0;
            const bool   bLast     = pointIndex + 1 == pointCount;
            const size_t prevIndex = bFirst ? ( bClosed ? pointCount - 1 : 0 ) : pointIndex - 1;
            const size_t nextIndex = bLast ? ( bClosed ? 0 : pointIndex ) : pointIndex + 1;
            Point&       point     = _listPoint[pointIndex];
            point._tangent         = CoasterTrackInternal::normalizeOr( _listPoint[nextIndex]._position - _listPoint[prevIndex]._position, float3{ 0.0f, 0.0f, 1.0f } );
            point._up              = CoasterTrackInternal::orthogonalize( point._up, point._tangent, previousUp );
            previousUp             = point._up;
        }
    }

    float32 CoasterTrack::wrapDistance( float32 distance ) const { return ArcLengthUtil::wrapDistance( distance, _length, _bClosed == SW_TRUE ); }

    CoasterTrackFrame CoasterTrack::sample( float32 distance ) const
    {
        CoasterTrackFrame frame;
        const size_t      pointCount = _listPoint.size();
        if ( pointCount < 2 )
        {
            if ( pointCount == 1 )
                frame._position = _listPoint[0]._position;
            return frame;
        }

        const ArcLengthSpan span  = ArcLengthUtil::findSpan( _listPoint, &Point::_distance, wrapDistance( distance ), _length, _bClosed == SW_TRUE );
        const Point&        from  = _listPoint[span._index];
        const Point&        to    = _listPoint[span._nextIndex];
        const float32       alpha = span._alpha;

        frame._position = from._position + ( to._position - from._position ) * alpha;
        frame._forward  = CoasterTrackInternal::normalizeOr( from._tangent + ( to._tangent - from._tangent ) * alpha, from._tangent );
        const float3 up = CoasterTrackInternal::normalizeOr( from._up + ( to._up - from._up ) * alpha, from._up );
        frame._up       = CoasterTrackInternal::orthogonalize( up, frame._forward, from._up );
        frame._right    = frame._up.cross( frame._forward );
        frame._flags    = from._flags;
        return frame;
    }

    // ------------------------------------------------------------------------------
    // CoasterTrackBuilder
    // ------------------------------------------------------------------------------
    CoasterTrackBuilder::CoasterTrackBuilder()
        : _listPoint{}
        , _startPosition{}
        , _position{}
        , _startHeading{ 0.0f }
        , _heading{ 0.0f }
    {
        reset( float3{ 0.0f, 0.0f, 0.0f }, 0.0f );
    }

    void CoasterTrackBuilder::reset( const float3& startPosition, float32 startHeadingDegrees )
    {
        _listPoint.clear();
        _startPosition = startPosition;
        _position      = startPosition;
        _startHeading  = startHeadingDegrees * MathUtil::kDegreeToRadian;
        _heading       = _startHeading;
        pushPoint( _position, computeForward(), 0.0f, CoasterSegmentFlag::kNone );
    }

    void CoasterTrackBuilder::appendPiece( const CoasterTrackPiece& piece )
    {
        // 시작점은 첫 조각의 구간이다 — 스테이션에서 멈춰 출발하는 열차가 그 자리의 표시(스테이션 구동)를 받아야 움직인다.
        if ( _listPoint.size() == 1 )
            _listPoint[0]._flags = CoasterTrackInternal::flagsOf( piece._type );
        switch ( piece._type )
        {
            case CoasterPieceType::TurnLeft:
            {
                appendTurn( piece, -1.0f );
                break;
            }
            case CoasterPieceType::TurnRight:
            {
                appendTurn( piece, 1.0f );
                break;
            }
            case CoasterPieceType::Loop:
            {
                appendLoop( piece );
                break;
            }
            case CoasterPieceType::Station:
            case CoasterPieceType::Straight:
            case CoasterPieceType::LiftHill:
            case CoasterPieceType::Drop:
            case CoasterPieceType::Hill:
            case CoasterPieceType::Brakes:
            case CoasterPieceType::Booster:
            {
                appendProfile( piece._length, piece._height, piece._type, CoasterTrackInternal::flagsOf( piece._type ) );
                break;
            }
        }
    }

    void CoasterTrackBuilder::appendPieces( const vector<CoasterTrackPiece>& listPiece )
    {
        for ( const CoasterTrackPiece& piece : listPiece )
        {
            appendPiece( piece );
        }
    }

    CoasterTrack CoasterTrackBuilder::makeTrack( bool bCloseCircuit ) const
    {
        CoasterTrack track;
        for ( const CoasterTrack::Point& point : _listPoint )
        {
            track.appendPoint( point._position, point._up, point._flags );
        }

        const size_t pointCount = _listPoint.size();
        if ( bCloseCircuit && pointCount >= 3 )
        {
            // 끝 → 시작 에르미트 연결. 끝 · 시작의 접선을 그대로 이어 꺾임 없이 닫는다.
            const float3  endPosition   = _listPoint.back()._position;
            const float3  startPosition = _listPoint.front()._position;
            const float32 gap           = float3::getDistance( endPosition, startPosition );
            if ( gap > 0.5f )
            {
                const float3 endTangent   = CoasterTrackInternal::normalizeOr( endPosition - _listPoint[pointCount - 2]._position, computeForward() );
                const float3 startTangent = CoasterTrackInternal::normalizeOr( _listPoint[1]._position - startPosition, float3{ 0.0f, 0.0f, 1.0f } );
                // 끝이 시작점을 이미 지나 있으면(시작점이 끝의 뒤) 연결 곡선이 되접혀 날카롭게 꺾인다 — 레이아웃이 틀렸다고 알린다.
                if ( ( startPosition - endPosition ).dot( endTangent ) < 0.0f )
                    SW_LOG_WARNING( "Coaster layout ends %# m past its station - the closing connector folds back; end the layout behind the station", gap );
                const float32 tangentScale = gap * 1.2f;
                const uint32  segmentCount = static_cast<uint32>( MathUtil::ceil( gap * 1.6f / kSampleSpacing ) ) + 1u;
                for ( uint32 segmentIndex = 1; segmentIndex < segmentCount; ++segmentIndex )
                {
                    const float32 u        = static_cast<float32>( segmentIndex ) / static_cast<float32>( segmentCount );
                    const float32 u2       = u * u;
                    const float32 u3       = u2 * u;
                    const float32 h00      = 2.0f * u3 - 3.0f * u2 + 1.0f;
                    const float32 h10      = u3 - 2.0f * u2 + u;
                    const float32 h01      = -2.0f * u3 + 3.0f * u2;
                    const float32 h11      = u3 - u2;
                    const float3  position = endPosition * h00 + endTangent * ( h10 * tangentScale ) + startPosition * h01 + startTangent * ( h11 * tangentScale );
                    track.appendPoint( position, float3{ 0.0f, 1.0f, 0.0f }, CoasterSegmentFlag::kNone );
                }
            }
        }
        track.finalize( bCloseCircuit );
        return track;
    }

    float3 CoasterTrackBuilder::computeForward() const
    {
        return CoasterTrackInternal::forwardOf( _heading );
    }

    void CoasterTrackBuilder::appendProfile( float32 length, float32 height, CoasterPieceType type, uint8 flags )
    {
        const float32 horizontalLength = MathUtil::max( 0.1f, length );
        float32       rise             = 0.0f;
        if ( type == CoasterPieceType::LiftHill )
            rise = MathUtil::abs( height );
        else if ( type == CoasterPieceType::Drop )
            rise = -MathUtil::abs( height );
        const bool bHill = type == CoasterPieceType::Hill;

        // 경사가 있으면 실제 곡선이 길다 — 간격이 넓어지지 않게 높이만큼 점을 더 둔다.
        const float32 estimatedLength = horizontalLength + MathUtil::abs( height ) * 1.6f;
        const uint32  stepCount       = static_cast<uint32>( MathUtil::ceil( estimatedLength / kSampleSpacing ) );
        const float3  start           = _position;
        const float3  forward         = computeForward();
        for ( uint32 stepIndex = 1; stepIndex <= stepCount; ++stepIndex )
        {
            const float32 t     = static_cast<float32>( stepIndex ) / static_cast<float32>( stepCount );
            float32       y     = 0.0f;
            float32       slope = 0.0f; // dy / d(수평 거리)
            if ( bHill )
            {
                const float32 sine = MathUtil::sin( MathUtil::kPi * t );
                y                  = height * sine * sine;
                slope              = height * MathUtil::kPi * MathUtil::sin( 2.0f * MathUtil::kPi * t ) / horizontalLength;
            }
            else
            {
                y     = rise * MathUtil::smoothstep( 0.0f, 1.0f, t );
                slope = rise * CoasterTrackInternal::smoothStepSlope( t ) / horizontalLength;
            }
            const float3 position = start + forward * ( t * horizontalLength ) + float3{ 0.0f, y, 0.0f };
            pushPoint( position, forward + float3{ 0.0f, slope, 0.0f }, 0.0f, flags );
        }
        _position = start + forward * horizontalLength + float3{ 0.0f, bHill ? 0.0f : rise, 0.0f };
    }

    void CoasterTrackBuilder::appendTurn( const CoasterTrackPiece& piece, float32 directionSign )
    {
        const float32 radius       = MathUtil::max( 1.0f, piece._radius );
        const float32 totalAngle   = MathUtil::max( 1.0f, MathUtil::abs( piece._angle ) ) * MathUtil::kDegreeToRadian;
        const float32 bank         = piece._bank * MathUtil::kDegreeToRadian * directionSign;
        const float32 rise         = piece._height;
        const float3  start        = _position;
        const float32 startHeading = _heading;
        const float3  center       = start + CoasterTrackInternal::rightOf( startHeading ) * ( radius * directionSign );
        const uint32  stepCount    = static_cast<uint32>( MathUtil::ceil( ( radius * totalAngle + MathUtil::abs( rise ) ) / kSampleSpacing ) );

        for ( uint32 stepIndex = 1; stepIndex <= stepCount; ++stepIndex )
        {
            const float32 u       = static_cast<float32>( stepIndex ) / static_cast<float32>( stepCount );
            const float32 heading = startHeading + directionSign * totalAngle * u;
            const float3  flat    = center - CoasterTrackInternal::rightOf( heading ) * ( radius * directionSign );
            const float32 y       = rise * MathUtil::smoothstep( 0.0f, 1.0f, u );
            // 접선(각도에 대한 미분) — 수평은 반지름 × 앞, 수직은 높이 곡선의 기울기.
            const float3 tangent = CoasterTrackInternal::forwardOf( heading ) * radius +
                                   float3{ 0.0f, rise * CoasterTrackInternal::smoothStepSlope( u ) / totalAngle, 0.0f };
            // 뱅크는 들어가며 기울고 나오며 바로 선다(앞뒤 20% 에서 부드럽게) — 조각 경계에서 위 벡터가 튀지 않는다.
            const float32 edge       = MathUtil::min( u, 1.0f - u ) / 0.2f;
            const float32 bankFactor = MathUtil::smoothstep( 0.0f, 1.0f, MathUtil::clamp( edge, 0.0f, 1.0f ) );
            pushPoint( flat + float3{ 0.0f, y, 0.0f }, tangent, bank * bankFactor, CoasterSegmentFlag::kNone );
        }
        _heading  = startHeading + directionSign * totalAngle;
        _position = center - CoasterTrackInternal::rightOf( _heading ) * ( radius * directionSign ) + float3{ 0.0f, rise, 0.0f };
    }

    void CoasterTrackBuilder::appendLoop( const CoasterTrackPiece& piece )
    {
        // 클로소이드(물방울) 루프 — 진행 각 φ 에서의 곡률 반지름이 아래는 크고(1.4R) 위는 작다(0.6R). 원형 루프는 위를 넘을 속도를 내려면 아래에서
        // G 가 너무 크다(실제 코스터가 원형을 쓰지 않는 이유). 높이는 2R, 앞으로 π·0.4R 만큼 나아가 끝난다.
        const float32 radius       = MathUtil::max( 2.0f, piece._radius );
        const float32 bottomRadius = radius * 1.4f;
        const float32 topRadius    = radius * 0.6f;
        const float32 width        = piece._width;
        const float3  start        = _position;
        const float3  forward      = computeForward();
        const float3  right        = CoasterTrackInternal::rightOf( _heading );
        const float3  worldUp{ 0.0f, 1.0f, 0.0f };
        const float32 fullTurn  = 2.0f * MathUtil::kPi;
        const uint32  stepCount = static_cast<uint32>( MathUtil::ceil( fullTurn * radius * 1.1f / kSampleSpacing ) ) * 2u; // 적분 간격 — 점은 이보다 성기게 둔다
        const float32 stepAngle = fullTurn / static_cast<float32>( stepCount );

        float32 along  = 0.0f; // 앞으로 간 거리
        float32 height = 0.0f;
        for ( uint32 stepIndex = 1; stepIndex <= stepCount; ++stepIndex )
        {
            // 중점 규칙으로 적분한다: 이 칸의 가운데 각에서의 반지름 × 각 = 호 길이.
            const float32 middleAngle = ( static_cast<float32>( stepIndex ) - 0.5f ) * stepAngle;
            const float32 curveRadius = topRadius + ( bottomRadius - topRadius ) * ( MathUtil::cos( middleAngle ) + 1.0f ) * 0.5f;
            along += curveRadius * stepAngle * MathUtil::cos( middleAngle );
            height += curveRadius * stepAngle * MathUtil::sin( middleAngle );
            if ( ( stepIndex % 2u ) != 0u && stepIndex != stepCount )
                continue;

            const float32 angle    = static_cast<float32>( stepIndex ) * stepAngle;
            const float32 progress = angle / fullTurn;
            const float3  position = start + forward * along + worldUp * height + right * ( width * MathUtil::smoothstep( 0.0f, 1.0f, progress ) );
            // 탑승자의 위는 곡률 중심 쪽 — 꼭대기에서 아래를 향한다(뒤집힘).
            CoasterTrack::Point point;
            point._position = position;
            point._up       = forward * ( -MathUtil::sin( angle ) ) + worldUp * MathUtil::cos( angle );
            point._flags    = CoasterSegmentFlag::kNone;
            _listPoint.push_back( point );
        }
        _position = start + forward * along + right * width;
    }

    void CoasterTrackBuilder::pushPoint( const float3& position, const float3& tangent, float32 bank, uint8 flags )
    {
        const float3 worldUp{ 0.0f, 1.0f, 0.0f };
        const float3 unitTangent = CoasterTrackInternal::normalizeOr( tangent, computeForward() );
        float3       up          = CoasterTrackInternal::orthogonalize( worldUp, unitTangent, worldUp );
        if ( bank != 0.0f )
        {
            const float3 right = up.cross( unitTangent );
            up                 = up * MathUtil::cos( bank ) + right * MathUtil::sin( bank );
        }

        CoasterTrack::Point point;
        point._position = position;
        point._up       = up;
        point._flags    = flags;
        _listPoint.push_back( point );
    }

    // ------------------------------------------------------------------------------
    // CoasterLayoutCatalog
    // ------------------------------------------------------------------------------
    CoasterLayoutCatalog::CoasterLayoutCatalog()
        : _catalog{}
    {
    }

    void CoasterLayoutCatalog::addLayout( const CoasterLayoutDef& layout )
    {
        (void)_catalog.add( layout );
    }

    uint32 CoasterLayoutCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode layoutNode = root.findChild( "Layout" ); layoutNode; layoutNode = layoutNode.findNextSibling( "Layout" ) )
        {
            const utf8* pID = GameDataXML::findRequiredID( layoutNode, sourceName );
            if ( pID == nullptr )
                continue;
            CoasterLayoutDef layout;
            layout._id          = hashed_string( pID );
            const utf8* pName   = layoutNode.findAttribute( "name" );
            layout._name        = pName != nullptr ? pName : pID;
            layout._startHeight = layoutNode.getAttributeFloat( "startHeight", 1.0f );

            for ( XMLNode pieceNode = layoutNode.findChild( "Piece" ); pieceNode; pieceNode = pieceNode.findNextSibling( "Piece" ) )
            {
                CoasterTrackPiece piece;
                const utf8*       pType = pieceNode.findAttribute( "type" );
                if ( pType == nullptr || parseCoasterPieceType( string_view( pType ), piece._type ) == false )
                {
                    SW_LOG_WARNING( "%#: layout '%#' has an unknown piece type '%#' - skipped", sourceName, pID, pType != nullptr ? pType : "" );
                    continue;
                }
                piece._length = pieceNode.getAttributeFloat( "length", piece._length );
                piece._height = pieceNode.getAttributeFloat( "height", piece._height );
                piece._radius = pieceNode.getAttributeFloat( "radius", piece._radius );
                piece._angle  = pieceNode.getAttributeFloat( "angle", piece._angle );
                piece._bank   = pieceNode.getAttributeFloat( "bank", piece._bank );
                piece._width  = pieceNode.getAttributeFloat( "width", piece._width );
                layout._listPiece.push_back( piece );
            }
            if ( layout._listPiece.empty() )
            {
                SW_LOG_WARNING( "%#: layout '%#' has no pieces - skipped", sourceName, pID );
                continue;
            }
            addLayout( layout );
            ++loadedCount;
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Layout> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
