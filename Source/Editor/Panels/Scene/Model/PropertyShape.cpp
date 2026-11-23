#include "pch.h"

#include "Editor/Panels/Scene/Model/PropertyShape.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

namespace sw::editor
{
    namespace
    {
        struct PropertyShapeInternal
        {
            /** @brief 화살표 방향이 틀 Y 와 나란하다고 보는 문턱(1 - |cos|)입니다. */
            static constexpr float32 kParallelLimit = 1.0e-6f;
            /** @brief 화살촉 폭 ÷ 화살촉 길이입니다. */
            static constexpr float32 kArrowHeadWidthRatio = 0.4f;
            /** @brief 캡슐 끝 반원 하나를 긋는 선분 수입니다. */
            static constexpr uint32 kArcSegmentCount = PropertyShapeValue::kCircleSegmentCount / 2;
            /** @brief 바닥 원점 캡슐의 높이 손잡이 거리 ÷ (반 높이 + 반지름)입니다 — 바닥에서 꼭대기까지. */
            static constexpr float32 kBaseHeightSpan = 2.0f;

            /** @brief 틀의 두 축 @p axisA · @p axisB 가 놓는 평면에 중심 @p center, 반지름 @p radius 의 원을 긋습니다. */
            static void addCircle( const float3& center, const float3& axisA, const float3& axisB, float32 radius, const EditorVisualizerLine& prototype,
                                   EditorVisualizerDrawList& outDrawList )
            {
                float3 arrPoint[PropertyShapeValue::kCircleSegmentCount];
                for ( uint32 index = 0; index < PropertyShapeValue::kCircleSegmentCount; ++index )
                {
                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( index ) / static_cast<float32>( PropertyShapeValue::kCircleSegmentCount );
                    arrPoint[index]     = center + axisA * ( MathUtil::cos( angle ) * radius ) + axisB * ( MathUtil::sin( angle ) * radius );
                }
                outDrawList.addLoop( arrPoint, PropertyShapeValue::kCircleSegmentCount, prototype );
            }

            /** @brief @p axisA 에서 @p axisB 쪽을 지나 -@p axisA 까지의 반원(열린 꺾은선)을 긋습니다. */
            static void addHalfCircle( const float3& center, const float3& axisA, const float3& axisB, float32 radius, const EditorVisualizerLine& prototype,
                                       EditorVisualizerDrawList& outDrawList )
            {
                float3 previous = center + axisA * radius;
                for ( uint32 index = 1; index <= kArcSegmentCount; ++index )
                {
                    const float32 angle   = MathUtil::kPi * static_cast<float32>( index ) / static_cast<float32>( kArcSegmentCount );
                    const float3  current = center + axisA * ( MathUtil::cos( angle ) * radius ) + axisB * ( MathUtil::sin( angle ) * radius );
                    addSegment( previous, current, prototype, outDrawList );
                    previous = current;
                }
            }

            static void addSegment( const float3& from, const float3& to, const EditorVisualizerLine& prototype, EditorVisualizerDrawList& outDrawList )
            {
                EditorVisualizerLine line = prototype;
                line._from                = from;
                line._to                  = to;
                outDrawList.addLine( line );
            }

            /** @brief 원뿔 반각을 그릴 · 끌 수 있는 범위로 자릅니다. */
            static float32 clampConeAngle( float32 angle ) { return MathUtil::clamp( angle, 0.0f, PropertyShapeValue::kMaxConeAngle ); }

            /** @brief 손잡이 하나를 만듭니다(틀 · 대상 · 종류 · 축 · 부호 · 축 시작점 · 방향 · 거리). */
            static PropertyShapeHandle makeHandle( const PropertyShapeValue& value, uint64 componentID, const hashed_string& property, PropertyShapeHandleKind kind, uint32 axis,
                                                   float32 sign, const float3& center, const float3& direction, float32 distance )
            {
                PropertyShapeHandle handle{};
                handle._frame       = value._frame;
                handle._vector      = value._vector;
                handle._number      = value.getWorldNumber();
                handle._range       = value.getWorldRange();
                handle._numberScale = value._numberScale;
                handle._rangeScale  = value._rangeScale;
                handle._vectorScale = value._vectorScale;
                handle._componentID = componentID;
                handle._property    = property;
                handle._kind        = kind;
                handle._axis        = axis;
                handle._sign        = sign;
                handle._center      = center;
                handle._direction   = direction;
                handle._position    = center + direction * distance;
                return handle;
            }

            /** @brief 중심 @p center 에서 틀 축 @p axis 의 양 · 음 쪽으로 거리 @p distance 의 손잡이 둘을 붙입니다. */
            static void addAxisPair( const PropertyShapeValue& value, uint64 componentID, const hashed_string& property, PropertyShapeHandleKind kind, uint32 axis,
                                     const float3& center, float32 distance, vector<PropertyShapeHandle>& outListHandle )
            {
                for ( const float32 sign : { 1.0f, -1.0f } )
                {
                    outListHandle.push_back( makeHandle( value, componentID, property, kind, axis, sign, center, value._frame.getAxis( axis ) * sign, distance ) );
                }
            }
        };
    } // namespace

    const float3& PropertyShapeFrame::getAxis( uint32 axis ) const
    {
        return axis == 0 ? _axisX : ( axis == 1 ? _axisY : _axisZ );
    }

    float3 PropertyShapeFrame::toWorldVector( const float3& local ) const
    {
        return _axisX * local._x + _axisY * local._y + _axisZ * local._z;
    }

    float3 PropertyShapeFrame::toLocalVector( const float3& world ) const
    {
        return float3{ world.dot( _axisX ), world.dot( _axisY ), world.dot( _axisZ ) };
    }

    bool PropertyShapeHandle::isSameHandle( const PropertyShapeHandle& other ) const
    {
        return _componentID == other._componentID && _property == other._property && _kind == other._kind && _elementIndex == other._elementIndex && _axis == other._axis && _sign == other._sign;
    }

    float32 PropertyShapeHandle::computeDraggedNumber( const Ray& ray ) const
    {
        // 끈 거리 · 잡은 값은 월드다 — 저장 값은 그 값의 배율로 나눈다(손잡이는 배율이 `kMinScale` 이상일 때만 선다).
        const float32 distance = Ray::computeClosestAxisParameter( ray, _center, _direction );
        switch ( _kind )
        {
            case PropertyShapeHandleKind::ConeAngle:
                return PropertyShapeInternal::clampConeAngle( MathUtil::atan2( MathUtil::max( distance, 0.0f ), _range ) );
            case PropertyShapeHandleKind::CapsuleHeight:
                return MathUtil::max( ( distance - _heightSpan * _number ) / _heightSpan, 0.0f ) / _rangeScale; // 꼭대기는 반구 끝 — 반지름을 빼면 원기둥 반 높이
            case PropertyShapeHandleKind::ConeRange:
                return MathUtil::max( distance, PropertyShapeValue::kMinExtent ) / _rangeScale;
            case PropertyShapeHandleKind::Radius:
            case PropertyShapeHandleKind::BoxExtent:
            case PropertyShapeHandleKind::ArrowTip:
            case PropertyShapeHandleKind::CylinderRadius:
                break;
        }
        return MathUtil::max( distance, PropertyShapeValue::kMinExtent ) / _numberScale;
    }

    float3 PropertyShapeHandle::computeDraggedVector( const Ray& ray ) const
    {
        float3 result = _vector;
        if ( _kind == PropertyShapeHandleKind::CylinderRadius )
        {
            result._x = MathUtil::max( Ray::computeClosestAxisParameter( ray, _center, _direction ), PropertyShapeValue::kMinExtent ) / _vectorScale._x;
            return result;
        }
        if ( _kind == PropertyShapeHandleKind::BoxExtent )
        {
            result.data()[_axis] = MathUtil::max( Ray::computeClosestAxisParameter( ray, _center, _direction ), PropertyShapeValue::kMinExtent ) / _vectorScale.data()[_axis];
            return result;
        }
        // 화살표 · 점: 잡은 자리를 지나 레이에 수직인 평면 위로 끈다(레이에서 잡은 자리에 가장 가까운 점). 화살표는 원점에서 그 점 쪽이 새 방향이다.
        const float32 alongRay = ( _position - ray._origin ).dot( ray._direction );
        const float3  target   = ray.getPoint( alongRay );
        if ( _kind == PropertyShapeHandleKind::Point )
        {
            const float3 local = _frame.toLocalVector( target - _frame._origin );
            return float3{ local._x / _vectorScale._x, local._y / _vectorScale._y, local._z / _vectorScale._z };
        }
        const float3  worldDir  = ( target - _frame._origin ).normalizeOr( _direction, PropertyShapeValue::kMinExtent );
        const float32 length    = _vector.getLength();
        const float32 keptScale = length > MathUtil::kEpsilon ? length : 1.0f;
        result                  = _frame.toLocalVector( worldDir ) * keptScale;
        return result;
    }

    float3 PropertyShapeValue::computeArrowDirection() const
    {
        return _frame.toWorldVector( _vector ).normalizeOr( _frame._axisZ, MathUtil::kEpsilon );
    }

    float3 PropertyShapeValue::computeCapsuleCenter() const
    {
        return _bBaseOrigin != 0 ? _frame._origin + _frame._axisY * ( getWorldRange() + getWorldNumber() ) : _frame._origin;
    }

    void PropertyShapeValue::addLines( const EditorVisualizerLine& prototype, EditorVisualizerDrawList& outDrawList ) const
    {
        using Internal       = PropertyShapeInternal;
        const float32 number = getWorldNumber();
        const float32 range  = getWorldRange();
        const float3  extent = getWorldVector();
        const float3& origin = _frame._origin;
        const float3& axisX  = _frame._axisX;
        const float3& axisY  = _frame._axisY;
        const float3& axisZ  = _frame._axisZ;
        switch ( _kind )
        {
            case PropertyShapeKind::Sphere:
                Internal::addCircle( origin, axisX, axisY, number, prototype, outDrawList );
                Internal::addCircle( origin, axisY, axisZ, number, prototype, outDrawList );
                Internal::addCircle( origin, axisZ, axisX, number, prototype, outDrawList );
                break;
            case PropertyShapeKind::Circle:
                Internal::addCircle( origin, axisX, axisY, number, prototype, outDrawList );
                break;
            case PropertyShapeKind::Box:
            {
                float3 arrCorner[8];
                for ( uint32 index = 0; index < 8; ++index )
                {
                    const float32 sx = ( index & 1u ) != 0 ? 1.0f : -1.0f;
                    const float32 sy = ( index & 2u ) != 0 ? 1.0f : -1.0f;
                    const float32 sz = ( index & 4u ) != 0 ? 1.0f : -1.0f;
                    arrCorner[index] = origin + _frame.toWorldVector( float3{ sx * extent._x, sy * extent._y, sz * extent._z } );
                }
                // 모서리는 한 비트만 다른 꼭짓점 쌍이다(꼭짓점 8 × 축 3 ÷ 2 = 12).
                for ( uint32 index = 0; index < 8; ++index )
                {
                    for ( uint32 bit = 1; bit < 8; bit <<= 1 )
                    {
                        if ( ( index & bit ) == 0 )
                            Internal::addSegment( arrCorner[index], arrCorner[index | bit], prototype, outDrawList );
                    }
                }
                break;
            }
            case PropertyShapeKind::Cone:
            {
                const float32 angle     = Internal::clampConeAngle( number );
                const float3  rimCenter = origin + axisZ * range;
                const float32 rimRadius = range * MathUtil::tan( angle );
                Internal::addCircle( rimCenter, axisX, axisY, rimRadius, prototype, outDrawList );
                for ( uint32 axis = 0; axis < 2; ++axis )
                {
                    const float3& side = _frame.getAxis( axis );
                    Internal::addSegment( origin, rimCenter + side * rimRadius, prototype, outDrawList );
                    Internal::addSegment( origin, rimCenter - side * rimRadius, prototype, outDrawList );
                }
                break;
            }
            case PropertyShapeKind::Capsule:
            {
                const float3 center = computeCapsuleCenter();
                const float3 top    = center + axisY * range;
                const float3 bottom = center - axisY * range;
                Internal::addCircle( top, axisZ, axisX, number, prototype, outDrawList );
                Internal::addCircle( bottom, axisZ, axisX, number, prototype, outDrawList );
                for ( const uint32 axis : { 0u, 2u } )
                {
                    const float3& side = _frame.getAxis( axis );
                    Internal::addSegment( bottom + side * number, top + side * number, prototype, outDrawList );
                    Internal::addSegment( bottom - side * number, top - side * number, prototype, outDrawList );
                    Internal::addHalfCircle( top, side, axisY, number, prototype, outDrawList );
                    Internal::addHalfCircle( bottom, side, -axisY, number, prototype, outDrawList );
                }
                break;
            }
            case PropertyShapeKind::Cylinder:
            {
                const float3 top    = origin + axisY * extent._y;
                const float3 bottom = origin - axisY * extent._y;
                Internal::addCircle( top, axisZ, axisX, extent._x, prototype, outDrawList );
                Internal::addCircle( bottom, axisZ, axisX, extent._x, prototype, outDrawList );
                for ( const uint32 axis : { 0u, 2u } )
                {
                    const float3& side = _frame.getAxis( axis );
                    Internal::addSegment( bottom + side * extent._x, top + side * extent._x, prototype, outDrawList );
                    Internal::addSegment( bottom - side * extent._x, top - side * extent._x, prototype, outDrawList );
                }
                break;
            }
            case PropertyShapeKind::Arrow:
            {
                const float3 direction = computeArrowDirection();
                const float3 tip       = origin + direction * kArrowLength;
                Internal::addSegment( origin, tip, prototype, outDrawList );
                // 화살촉 옆 두 축은 방향에 수직 — 방향이 틀 Y 와 나란하면 틀 X 로 짓는다.
                const float3  reference = MathUtil::abs( direction.dot( axisY ) ) > 1.0f - Internal::kParallelLimit ? axisX : axisY;
                const float3  headSideA = direction.cross( reference ).normalizeOr( axisX, MathUtil::kEpsilon );
                const float3  headSideB = direction.cross( headSideA ).normalizeOr( axisY, MathUtil::kEpsilon );
                const float32 headSize  = kArrowLength * kArrowHeadRatio;
                const float3  headBase  = tip - direction * headSize;
                const float32 headWidth = headSize * Internal::kArrowHeadWidthRatio;
                Internal::addSegment( tip, headBase + headSideA * headWidth, prototype, outDrawList );
                Internal::addSegment( tip, headBase - headSideA * headWidth, prototype, outDrawList );
                Internal::addSegment( tip, headBase + headSideB * headWidth, prototype, outDrawList );
                Internal::addSegment( tip, headBase - headSideB * headWidth, prototype, outDrawList );
                break;
            }
            case PropertyShapeKind::Points:
                for ( uint32 index = 1; index < static_cast<uint32>( _listPoint.size() ); ++index )
                {
                    Internal::addSegment( computeWorldPoint( index - 1 ), computeWorldPoint( index ), prototype, outDrawList );
                }
                break;
        }
    }

    float3 PropertyShapeValue::computeWorldPoint( uint32 index ) const
    {
        return _frame._origin + _frame.toWorldVector( float3::multiply( _listPoint[index], _vectorScale ) );
    }

    void PropertyShapeValue::collectHandles( uint64 componentID, const hashed_string& property, const hashed_string& rangeProperty, vector<PropertyShapeHandle>& outListHandle ) const
    {
        using Internal              = PropertyShapeInternal;
        const float32 smallestScale = MathUtil::min( MathUtil::min( _numberScale, _rangeScale ), MathUtil::min( _vectorScale._x, MathUtil::min( _vectorScale._y, _vectorScale._z ) ) );
        if ( smallestScale < kMinScale )
            return; // 배율 0 — 끈 거리를 저장 값으로 되돌릴 수 없다
        const float32 number = getWorldNumber();
        const float32 range  = getWorldRange();
        const float3  extent = getWorldVector();
        const float3& origin = _frame._origin;
        switch ( _kind )
        {
            case PropertyShapeKind::Sphere:
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::Radius, axis, origin, number, outListHandle );
                }
                break;
            case PropertyShapeKind::Circle:
                for ( uint32 axis = 0; axis < 2; ++axis )
                {
                    Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::Radius, axis, origin, number, outListHandle );
                }
                break;
            case PropertyShapeKind::Box:
                for ( uint32 axis = 0; axis < 3; ++axis )
                {
                    Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::BoxExtent, axis, origin, extent.data()[axis], outListHandle );
                }
                break;
            case PropertyShapeKind::Cone:
            {
                if ( range >= kMinExtent )
                {
                    const float3  rimCenter = origin + _frame._axisZ * range;
                    const float32 rimRadius = range * MathUtil::tan( Internal::clampConeAngle( number ) );
                    for ( uint32 axis = 0; axis < 2; ++axis )
                    {
                        Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::ConeAngle, axis, rimCenter, rimRadius, outListHandle );
                    }
                }
                outListHandle.push_back( Internal::makeHandle( *this, componentID, rangeProperty, PropertyShapeHandleKind::ConeRange, 2, 1.0f, origin, _frame._axisZ, range ) );
                break;
            }
            case PropertyShapeKind::Capsule:
            {
                const float3 center = computeCapsuleCenter();
                Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::Radius, 0, center, number, outListHandle );
                Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::Radius, 2, center, number, outListHandle );
                if ( _bBaseOrigin != 0 )
                {
                    // 바닥이 원점에 붙어 있다 — 꼭대기 손잡이 하나, 거리는 바닥에서 꼭대기까지.
                    PropertyShapeHandle height = Internal::makeHandle( *this, componentID, rangeProperty, PropertyShapeHandleKind::CapsuleHeight, 1, 1.0f, origin, _frame._axisY,
                                                                       Internal::kBaseHeightSpan * ( range + number ) );
                    height._heightSpan         = Internal::kBaseHeightSpan;
                    outListHandle.push_back( height );
                }
                else
                {
                    Internal::addAxisPair( *this, componentID, rangeProperty, PropertyShapeHandleKind::CapsuleHeight, 1, center, range + number, outListHandle );
                }
                break;
            }
            case PropertyShapeKind::Cylinder:
                Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::CylinderRadius, 0, origin, extent._x, outListHandle );
                Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::CylinderRadius, 2, origin, extent._x, outListHandle );
                Internal::addAxisPair( *this, componentID, property, PropertyShapeHandleKind::BoxExtent, 1, origin, extent._y, outListHandle ); // 반 높이 — float3 의 y
                break;
            case PropertyShapeKind::Arrow:
                outListHandle.push_back( Internal::makeHandle( *this, componentID, property, PropertyShapeHandleKind::ArrowTip, 2, 1.0f, origin, computeArrowDirection(), kArrowLength ) );
                break;
            case PropertyShapeKind::Points:
                for ( uint32 index = 0; index < static_cast<uint32>( _listPoint.size() ); ++index )
                {
                    PropertyShapeHandle handle = Internal::makeHandle( *this, componentID, property, PropertyShapeHandleKind::Point, 0, 1.0f, computeWorldPoint( index ), _frame._axisX, 0.0f );
                    handle._vector             = _listPoint[index];
                    handle._elementIndex       = index;
                    outListHandle.push_back( handle );
                }
                break;
        }
    }
} // namespace sw::editor
