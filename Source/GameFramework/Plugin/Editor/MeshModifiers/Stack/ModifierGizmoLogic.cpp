/**
 * @file ModifierGizmoLogic.cpp
 * @brief 모디파이어 뷰포트 편집의 순수 계산(레이 거리 · 교점 · 끌기 이동량 · 범위 사각형 · 매개변수 분류)을 구현합니다.
 */
#include "pch.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/ModifierGizmoLogic.h"

#include "Core/Math/MathUtil.h"

#include <cmath>

namespace sw::editor
{
    namespace
    {
        struct ModifierGizmoLogicInternal
        {
            static utf8 toLower( utf8 value )
            {
                return ( value >= 'A' && value <= 'Z' ) ? static_cast<utf8>( value - 'A' + 'a' ) : value;
            }

            /** @brief 대소문자를 무시하고 @p text 안에 @p word 가 있는지 봅니다. */
            static bool containsIgnoreCase( string_view text, string_view word )
            {
                if ( word.empty() || text.size() < word.size() )
                    return false;
                for ( size_t start = 0; start + word.size() <= text.size(); ++start )
                {
                    size_t matched = 0;
                    while ( matched < word.size() && toLower( text[start + matched] ) == toLower( word[matched] ) )
                    {
                        ++matched;
                    }
                    if ( matched == word.size() )
                        return true;
                }
                return false;
            }

            static bool equalsIgnoreCase( string_view text, string_view word )
            {
                return text.size() == word.size() && containsIgnoreCase( text, word );
            }

            /** @brief 낱말 표의 한 줄입니다. 힌트에서 찾을 낱말과 이름 규칙이 같다. */
            struct RangeWord
            {
                const utf8*       _pWord;
                ModifierRangeKind _kind;
            };

            // "lower limit" · "upper limit" 이 "limit" 보다, "center" 가 짧은 낱말보다 먼저 맞도록 긴 것부터 둔다
            static constexpr RangeWord kArrRangeWord[] = {
                {"lower limit", ModifierRangeKind::LowerLimit},
                {"upper limit", ModifierRangeKind::UpperLimit},
                {  "direction",  ModifierRangeKind::Direction},
                {     "radius",     ModifierRangeKind::Radius},
                {     "center",     ModifierRangeKind::Center},
            };

            /** @brief 종류가 이 그리기에 맞는지 봅니다. */
            static bool isKindAccepted( ModifierRangeKind rangeKind, MeshModifierParamKind paramKind )
            {
                switch ( rangeKind )
                {
                    case ModifierRangeKind::Radius:
                    case ModifierRangeKind::LowerLimit:
                    case ModifierRangeKind::UpperLimit:
                        return paramKind == MeshModifierParamKind::Float;
                    case ModifierRangeKind::Center:
                        return paramKind == MeshModifierParamKind::Vector3;
                    case ModifierRangeKind::Direction:
                        return paramKind == MeshModifierParamKind::Float || paramKind == MeshModifierParamKind::Vector3;
                    case ModifierRangeKind::None:
                        return false;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float32 ModifierGizmoLogic::computeClosestLineParameter( const float3& rayOrigin, const float3& rayDirection, const float3& lineOrigin, const float3& lineDirection )
    {
        // 두 직선의 최근접점(Sunday): w0 = 레이 원점 - 직선 원점
        const float3  offset      = rayOrigin - lineOrigin;
        const float32 rayDot      = rayDirection.dot( rayDirection );
        const float32 crossDot    = rayDirection.dot( lineDirection );
        const float32 lineDot     = lineDirection.dot( lineDirection );
        const float32 rayOffset   = rayDirection.dot( offset );
        const float32 lineOffset  = lineDirection.dot( offset );
        const float32 denominator = rayDot * lineDot - crossDot * crossDot;
        if ( MathUtil::abs( denominator ) <= MathUtil::kEpsilon * MathUtil::max( rayDot * lineDot, 1.0f ) )
            return 0.0f;
        return ( rayDot * lineOffset - crossDot * rayOffset ) / denominator;
    }

    float32 ModifierGizmoLogic::computeRaySegmentDistance( const float3& rayOrigin, const float3& rayDirection, const float3& segmentA, const float3& segmentB )
    {
        const float3  segment = segmentB - segmentA;
        const float32 length2 = segment.dot( segment );
        if ( length2 <= MathUtil::kEpsilonSquared )
            return computeRayPointDistance( rayOrigin, rayDirection, segmentA );
        // 직선 최근접 매개변수를 선분으로 자른 점과, 두 끝점 중 가장 가까운 것을 본다(레이 뒤쪽 자르기까지 함께 덮는다)
        const float32 parameter = MathUtil::saturate( computeClosestLineParameter( rayOrigin, rayDirection, segmentA, segment ) );
        const float32 middle    = computeRayPointDistance( rayOrigin, rayDirection, segmentA + segment * parameter );
        const float32 endA      = computeRayPointDistance( rayOrigin, rayDirection, segmentA );
        const float32 endB      = computeRayPointDistance( rayOrigin, rayDirection, segmentB );
        return MathUtil::min( middle, MathUtil::min( endA, endB ) );
    }

    float32 ModifierGizmoLogic::computeRayPointDistance( const float3& rayOrigin, const float3& rayDirection, const float3& point )
    {
        const float32 along = MathUtil::max( ( point - rayOrigin ).dot( rayDirection ), 0.0f );
        return ( rayOrigin + rayDirection * along - point ).getLength();
    }

    bool ModifierGizmoLogic::tryIntersectRayPlane( const float3& rayOrigin, const float3& rayDirection, const float3& planePoint, const float3& planeNormal,
                                                   float3& outPoint )
    {
        const float32 facing = rayDirection.dot( planeNormal );
        if ( MathUtil::abs( facing ) <= MathUtil::kEpsilon )
            return false;
        const float32 distance = ( planePoint - rayOrigin ).dot( planeNormal ) / facing;
        if ( distance < 0.0f )
            return false;
        outPoint = rayOrigin + rayDirection * distance;
        return true;
    }

    bool ModifierGizmoLogic::tryComputePlaneDragDelta( const float3& startRayOrigin, const float3& startRayDirection, const float3& rayOrigin,
                                                       const float3& rayDirection, const float3& planePoint, const float3& planeNormal, float3& outDelta )
    {
        float3 startPoint{ 0.0f, 0.0f, 0.0f };
        float3 nowPoint{ 0.0f, 0.0f, 0.0f };
        if ( tryIntersectRayPlane( startRayOrigin, startRayDirection, planePoint, planeNormal, startPoint ) == false ||
             tryIntersectRayPlane( rayOrigin, rayDirection, planePoint, planeNormal, nowPoint ) == false )
            return false;
        outDelta = nowPoint - startPoint;
        return true;
    }

    int32 ModifierGizmoLogic::findPointOnRay( const float3& rayOrigin, const float3& rayDirection, const vector<float3>& listPoint, float32 radius )
    {
        int32   found     = -1;
        float32 bestAlong = 0.0f;
        for ( size_t index = 0; index < listPoint.size(); ++index )
        {
            const float32 along = ( listPoint[index] - rayOrigin ).dot( rayDirection );
            if ( along < 0.0f || computeRayPointDistance( rayOrigin, rayDirection, listPoint[index] ) > radius )
                continue;
            if ( found < 0 || along < bestAlong )
            {
                found     = static_cast<int32>( index );
                bestAlong = along;
            }
        }
        return found;
    }

    float32 ModifierGizmoLogic::computeSignedAngleDegree( const float3& from, const float3& to, const float3& axis )
    {
        const float32 axisLength = axis.getLength();
        if ( axisLength <= MathUtil::kEpsilon )
            return 0.0f;
        const float3 unitAxis      = axis / axisLength;
        const float3 fromProjected = from - unitAxis * from.dot( unitAxis );
        const float3 toProjected   = to - unitAxis * to.dot( unitAxis );
        if ( fromProjected.getLengthSquared() <= MathUtil::kEpsilonSquared || toProjected.getLengthSquared() <= MathUtil::kEpsilonSquared )
            return 0.0f;
        const float32 sine   = unitAxis.dot( fromProjected.cross( toProjected ) );
        const float32 cosine = fromProjected.dot( toProjected );
        return MathUtil::toDegree( ::atan2f( sine, cosine ) );
    }

    bool ModifierGizmoLogic::tryMakeLimitQuad( const MeshModifierBounds& bounds, uint32 axisIndex, float32 fraction, vector<float3>& outListCorner )
    {
        outListCorner.clear();
        if ( bounds.isValid() == false )
            return false;
        const uint32  axis       = MathUtil::min( axisIndex, 2u );
        const uint32  axisU      = ( axis + 1 ) % 3;
        const uint32  axisV      = ( axis + 2 ) % 3;
        const float32 coordinate = MeshModifierUtil::getComponent( bounds._min, axis ) + fraction * MeshModifierUtil::getComponent( bounds.getSize(), axis );
        const float32 arrU[]     = { MeshModifierUtil::getComponent( bounds._min, axisU ), MeshModifierUtil::getComponent( bounds._max, axisU ),
                                     MeshModifierUtil::getComponent( bounds._max, axisU ), MeshModifierUtil::getComponent( bounds._min, axisU ) };
        const float32 arrV[]     = { MeshModifierUtil::getComponent( bounds._min, axisV ), MeshModifierUtil::getComponent( bounds._min, axisV ),
                                     MeshModifierUtil::getComponent( bounds._max, axisV ), MeshModifierUtil::getComponent( bounds._max, axisV ) };
        for ( uint32 corner = 0; corner < 4; ++corner )
        {
            float3 point{ 0.0f, 0.0f, 0.0f };
            MeshModifierUtil::setComponent( point, axis, coordinate );
            MeshModifierUtil::setComponent( point, axisU, arrU[corner] );
            MeshModifierUtil::setComponent( point, axisV, arrV[corner] );
            outListCorner.push_back( point );
        }
        return true;
    }

    float3 ModifierGizmoLogic::computeDirectionVector( uint32 axisIndex, float32 angleDegree )
    {
        const uint32  axis   = MathUtil::min( axisIndex, 2u );
        const float32 radian = MathUtil::toRadian( angleDegree );
        float3        direction{ 0.0f, 0.0f, 0.0f };
        MeshModifierUtil::setComponent( direction, ( axis + 1 ) % 3, ::cosf( radian ) );
        MeshModifierUtil::setComponent( direction, ( axis + 2 ) % 3, ::sinf( radian ) );
        return direction;
    }

    int32 ModifierGizmoLogic::findMirrorIndex2D( int32 index, int32 countX, int32 countY, uint32 axisIndex )
    {
        const bool bInvalidMirrorIndex = countX <= 0 || countY <= 0 || index < 0 || index >= countX * countY;
        if ( bInvalidMirrorIndex )
            return -1;
        const int32 indexX = index % countX;
        const int32 indexY = index / countX;
        if ( axisIndex == 0 )
            return ( countX - 1 - indexX ) + countX * indexY;
        if ( axisIndex == 1 )
            return indexX + countX * ( countY - 1 - indexY );
        return -1;
    }

    float3 ModifierGizmoLogic::applyRotationDelta( const float3& eulerDegree, uint32 axisIndex, float32 deltaDegree )
    {
        // 지금 회전 행렬의 열(모디파이어 단위 축의 기준 공간 방향)을 기즈모 정의 그대로 구한다
        MeshModifierGizmo gizmo{};
        gizmo._rotationDegree = eulerDegree;
        float3 arrColumn[3]   = { gizmo.toBaseDirection( float3{ 1.0f, 0.0f, 0.0f } ), gizmo.toBaseDirection( float3{ 0.0f, 1.0f, 0.0f } ),
                                  gizmo.toBaseDirection( float3{ 0.0f, 0.0f, 1.0f } ) };
        // 기준 축 둘레 회전(로드리게스)을 각 열에 건다: 새 회전 = 축 회전 · 지금 회전
        float3 axis{ 0.0f, 0.0f, 0.0f };
        MeshModifierUtil::setComponent( axis, MathUtil::min( axisIndex, 2u ), 1.0f );
        const float32 radian   = MathUtil::toRadian( deltaDegree );
        const float32 cosValue = ::cosf( radian );
        const float32 sinValue = ::sinf( radian );
        for ( float3& column : arrColumn )
        {
            column = column * cosValue + axis.cross( column ) * sinValue + axis * ( axis.dot( column ) * ( 1.0f - cosValue ) );
        }
        // R = Rz · Ry · Rx 풀기: 원소 r(행, 열) = arrColumn[열] 의 행 성분
        const float32 r00    = arrColumn[0]._x;
        const float32 r10    = arrColumn[0]._y;
        const float32 r20    = arrColumn[0]._z;
        const float32 r01    = arrColumn[1]._x;
        const float32 r11    = arrColumn[1]._y;
        const float32 r21    = arrColumn[1]._z;
        const float32 r22    = arrColumn[2]._z;
        const float32 cosY   = ::sqrtf( r00 * r00 + r10 * r10 );
        const float32 angleY = ::atan2f( -r20, cosY );
        float32       angleX = 0.0f;
        float32       angleZ = 0.0f;
        if ( cosY > 1e-4f )
        {
            angleX = ::atan2f( r21, r22 );
            angleZ = ::atan2f( r10, r00 );
        }
        else
        {
            // 짐벌: X 와 Z 가 한 축을 돈다. X = 0 으로 두면 R = Rz · Ry 라 r01 = -sin z, r11 = cos z
            angleZ = ::atan2f( -r01, r11 );
        }
        return float3{ MathUtil::toDegree( angleX ), MathUtil::toDegree( angleY ), MathUtil::toDegree( angleZ ) };
    }

    ModifierRangeKind ModifierGizmoLogic::computeRangeKind( const MeshModifierParamDef& def )
    {
        using Internal = ModifierGizmoLogicInternal;
        if ( def._pHint != nullptr )
        {
            for ( const Internal::RangeWord& word : Internal::kArrRangeWord )
            {
                if ( Internal::containsIgnoreCase( def._pHint, word._pWord ) )
                    return Internal::isKindAccepted( word._kind, def._kind ) ? word._kind : ModifierRangeKind::None;
            }
        }
        if ( def._pName == nullptr )
            return ModifierRangeKind::None;
        for ( const Internal::RangeWord& word : Internal::kArrRangeWord )
        {
            if ( Internal::equalsIgnoreCase( def._pName, word._pWord ) )
                return Internal::isKindAccepted( word._kind, def._kind ) ? word._kind : ModifierRangeKind::None;
        }
        return ModifierRangeKind::None;
    }
} // namespace sw::editor
