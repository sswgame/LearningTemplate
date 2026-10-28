/**
 * @file BezierAttractorModifier.cpp
 * @brief 3차 베지어 곡선(제어점 넷, 모디파이어 공간)으로 반경 안의 정점을 끌어당기는 모디파이어입니다.
 * @details 곡선을 kSegmentCount 개 선분으로 나눠 가장 가까운 점 q 를 찾고, 거리 d 가 반경보다 작으면
 *          w = 세기 × (1 - d / 반경)^감쇠 만큼 q 쪽으로 섞는다. 세기 기본값 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 베지어 끌개입니다. */
    class BezierAttractorModifier final : public MeshModifier
    {
    public:
        BezierAttractorModifier()
            : MeshModifier()
            , _point0Param{
                  addParam( MeshModifierParamDef{ "Point 0", MeshModifierParamKind::Vector3, -1000.0f, 1000.0f, float3{ 0.0f, -1.0f, 0.0f } }
                                .withLayout( "Curve", "Start point of the Bezier curve" ) )
        },
            _point1Param{ addParam( MeshModifierParamDef{ "Point 1", MeshModifierParamKind::Vector3, -1000.0f, 1000.0f, float3{ 1.0f, -0.33f, 0.0f } }.withLayout( "Curve", "First control point of the Bezier curve" ) ) },
            _point2Param{ addParam( MeshModifierParamDef{ "Point 2", MeshModifierParamKind::Vector3, -1000.0f, 1000.0f, float3{ -1.0f, 0.33f, 0.0f } }.withLayout( "Curve", "Second control point of the Bezier curve" ) ) },
            _point3Param{ addParam( MeshModifierParamDef{ "Point 3", MeshModifierParamKind::Vector3, -1000.0f, 1000.0f, float3{ 0.0f, 1.0f, 0.0f } }.withLayout( "Curve", "End point of the Bezier curve" ) ) },
            _strengthParam{ addParam( MeshModifierParamDef{ "Strength", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Attraction", "How far points are pulled toward the curve (1 = onto it)" ) ) },
            _radiusParam{ addParam( MeshModifierParamDef{ "Radius", MeshModifierParamKind::Float, 0.001f, 1000.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Attraction", "Distance from the curve within which points are attracted", "m" ) ) },
            _falloffParam{ addParam( MeshModifierParamDef{ "Falloff", MeshModifierParamKind::Float, 0.01f, 10.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Attraction", "Exponent shaping how the pull fades with distance" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Pulls points within a radius toward a cubic Bezier curve."; }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& ) const override
        {
            const float32 strength = getFloat( _strengthParam );
            if ( strength <= MathUtil::kEpsilon )
                return point._position;
            const float3  closest  = findClosestOnCurve( point._position );
            const float3  delta    = closest - point._position;
            const float32 distance = delta.getLength();
            const float32 radius   = getFloat( _radiusParam );
            if ( distance >= radius )
                return point._position;
            const float32 weight = strength * MathUtil::pow( 1.0f - distance / radius, getFloat( _falloffParam ) );
            return point._position + delta * MathUtil::saturate( weight );
        }

    private:
        static constexpr uint32 kSegmentCount = 64;

        float3 evaluateBezier( float32 t ) const
        {
            const float32 inverse = 1.0f - t;
            return getVector3( _point0Param ) * ( inverse * inverse * inverse ) + getVector3( _point1Param ) * ( 3.0f * inverse * inverse * t ) + getVector3( _point2Param ) * ( 3.0f * inverse * t * t ) + getVector3( _point3Param ) * ( t * t * t );
        }

        float3 findClosestOnCurve( const float3& point ) const
        {
            float3  previous     = evaluateBezier( 0.0f );
            float3  best         = previous;
            float32 bestDistance = ( previous - point ).getLengthSquared();
            for ( uint32 segment = 1; segment <= kSegmentCount; ++segment )
            {
                const float3  current       = evaluateBezier( static_cast<float32>( segment ) / static_cast<float32>( kSegmentCount ) );
                const float3  edge          = current - previous;
                const float32 edgeLength    = edge.getLengthSquared();
                const float32 ratio         = edgeLength > MathUtil::kEpsilon ? MathUtil::saturate( ( point - previous ).dot( edge ) / edgeLength ) : 0.0f;
                const float3  candidate     = previous + edge * ratio;
                const float32 candidateDist = ( candidate - point ).getLengthSquared();
                if ( candidateDist < bestDistance )
                {
                    bestDistance = candidateDist;
                    best         = candidate;
                }
                previous = current;
            }
            return best;
        }

        uint32 _point0Param;
        uint32 _point1Param;
        uint32 _point2Param;
        uint32 _point3Param;
        uint32 _strengthParam;
        uint32 _radiusParam;
        uint32 _falloffParam;
    };

    SW_MESH_MODIFIER( BezierAttractorModifier, "bezierattractor", "Bezier Attractor", "Curve" );
} // namespace sw::editor
