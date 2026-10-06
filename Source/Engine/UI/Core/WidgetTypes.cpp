#include "pch.h"

#include "Engine/UI/Core/WidgetTypes.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct WidgetTypesInternal
        {
            /** @brief 축이 퇴화했다고 보는 행렬식 크기입니다(UI 단위 제곱 — 1e-6 은 0.001 × 0.001 사각형). */
            static constexpr float32 kDegenerateDeterminant = 1.0e-6f;

            static bool isNear( const float2& lhs, const float2& rhs ) { return MathUtil::nearEqual( lhs._x, rhs._x ) && MathUtil::nearEqual( lhs._y, rhs._y ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    WidgetGeometry WidgetGeometry::makeAxisAligned( const float2& position, const float2& size )
    {
        WidgetGeometry geometry{};
        geometry._position    = position;
        geometry._size        = size;
        geometry._translation = position;
        return geometry;
    }

    float2 WidgetGeometry::transformPoint( const float2& local ) const
    {
        return float2{ _translation._x + local._x * _axisX._x + local._y * _axisY._x, _translation._y + local._x * _axisX._y + local._y * _axisY._y };
    }

    bool WidgetGeometry::inverseTransformPoint( const float2& screen, float2& outLocal ) const
    {
        // [axisX axisY] · local = screen - translation 을 2×2 역행렬로 푼다.
        const float32 determinant = _axisX._x * _axisY._y - _axisY._x * _axisX._y;
        if ( MathUtil::abs( determinant ) < WidgetTypesInternal::kDegenerateDeterminant )
            return false;
        const float32 deltaX = screen._x - _translation._x;
        const float32 deltaY = screen._y - _translation._y;
        outLocal._x          = ( deltaX * _axisY._y - deltaY * _axisY._x ) / determinant;
        outLocal._y          = ( _axisX._x * deltaY - _axisX._y * deltaX ) / determinant;
        return true;
    }

    bool WidgetGeometry::isAxisAligned() const
    {
        return _axisX._x == 1.0f && _axisX._y == 0.0f && _axisY._x == 0.0f && _axisY._y == 1.0f;
    }

    bool WidgetGeometry::operator==( const WidgetGeometry& other ) const
    {
        return WidgetTypesInternal::isNear( _position, other._position ) && WidgetTypesInternal::isNear( _size, other._size ) &&
               WidgetTypesInternal::isNear( _axisX, other._axisX ) && WidgetTypesInternal::isNear( _axisY, other._axisY ) &&
               WidgetTypesInternal::isNear( _translation, other._translation );
    }
} // namespace sw

namespace sw
{
    bool WidgetRenderTransform::isIdentity() const
    {
        return _translation._x == 0.0f && _translation._y == 0.0f && _scale._x == 1.0f && _scale._y == 1.0f && _shear._x == 0.0f && _shear._y == 0.0f &&
               _angleDegrees == 0.0f;
    }

    WidgetGeometry WidgetRenderTransform::applyTo( const WidgetGeometry& geometry ) const
    {
        if ( isIdentity() )
            return geometry;
        // 로컬 2×2 = 회전 · 기울임 · 배율(배율이 먼저). 열 벡터 a = 로컬 x 축, b = 로컬 y 축.
        const float32 radians = MathUtil::toRadian( _angleDegrees );
        const float32 cosine  = MathUtil::cos( radians );
        const float32 sine    = MathUtil::sin( radians );
        // 기울임 [1 shearX; shearY 1] × 배율 diag(scaleX, scaleY)
        const float2 shearedX{ _scale._x, _shear._y * _scale._x };
        const float2 shearedY{ _shear._x * _scale._y, _scale._y };
        const float2 localAxisX{ cosine * shearedX._x - sine * shearedX._y, sine * shearedX._x + cosine * shearedX._y };
        const float2 localAxisY{ cosine * shearedY._x - sine * shearedY._y, sine * shearedY._x + cosine * shearedY._y };
        // 부모 축으로 옮긴다: 새 축 = 부모 축 · 로컬 축.
        WidgetGeometry result = geometry;
        result._axisX         = float2{ geometry._axisX._x * localAxisX._x + geometry._axisY._x * localAxisX._y, geometry._axisX._y * localAxisX._x + geometry._axisY._y * localAxisX._y };
        result._axisY         = float2{ geometry._axisX._x * localAxisY._x + geometry._axisY._x * localAxisY._y, geometry._axisX._y * localAxisY._x + geometry._axisY._y * localAxisY._y };
        // 피벗(로컬 점)이 제자리에 남도록: 새 원점 = 피벗 화면점 - 새 축 · 피벗 + 이동(부모 축으로).
        const float2 pivotLocal{ _pivot._x * geometry._size._x, _pivot._y * geometry._size._y };
        const float2 pivotScreen = geometry.transformPoint( pivotLocal );
        const float2 offsetScreen{ geometry._axisX._x * _translation._x + geometry._axisY._x * _translation._y,
                                   geometry._axisX._y * _translation._x + geometry._axisY._y * _translation._y };
        result._translation = float2{ pivotScreen._x - ( result._axisX._x * pivotLocal._x + result._axisY._x * pivotLocal._y ) + offsetScreen._x,
                                      pivotScreen._y - ( result._axisX._y * pivotLocal._x + result._axisY._y * pivotLocal._y ) + offsetScreen._y };
        return result;
    }

    bool WidgetRenderTransform::operator==( const WidgetRenderTransform& other ) const
    {
        return _translation._x == other._translation._x && _translation._y == other._translation._y && _scale._x == other._scale._x &&
               _scale._y == other._scale._y && _shear._x == other._shear._x && _shear._y == other._shear._y && _angleDegrees == other._angleDegrees &&
               _pivot._x == other._pivot._x && _pivot._y == other._pivot._y;
    }
} // namespace sw
