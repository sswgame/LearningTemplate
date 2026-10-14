/**
 * @file PageFlipModifier.cpp
 * @brief 종이를 넘기듯 한쪽 끝부터 원통에 감아 뒤집는 모디파이어입니다.
 * @details 종이 면의 법선 = 주축(기즈모 축), 넘기는 방향 = `Flip Axis`(주축과 같으면 다음 축). u = 넘기는 축 좌표, h = 주축 좌표.
 *          말림선 c = u 최대 - 진행도 · 폭(상자 기준). u ≤ c 인 점은 그대로, 넘어선 점은 호 길이 s = u - c 만큼 반지름 r 원통에 감긴다:
 *          s ≤ π r 이면 θ = s / r, u' = c + (r - h) sin θ, h' = r - (r - h) cos θ; 넘으면 뒤집혀 평평히 눕는다: u' = c - (s - π r), h' = 2r - h.
 *          진행도 0 이면 말림선이 끝이라 항등, 1 이면 종이 전체가 시작선(u 최소)을 넘어 뒤집힌다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 종이 넘김입니다. */
    class PageFlipModifier final : public MeshModifier
    {
    public:
        PageFlipModifier()
            : MeshModifier()
            , _progressParam{ addParam( MeshModifierParamDef{ "Progress", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _radiusParam{ addParam( MeshModifierParamDef{ "Curl Radius", MeshModifierParamKind::Float, 0.001f, 1000.0f, float3{ 0.25f, 0.0f, 0.0f } } ) }
            , _flipAxisParam{ addParam( MeshModifierParamDef{ "Flip Axis", MeshModifierParamKind::Axis, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
        {
        }

        bool supportsGPU() const override { return true; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            const float32 progress = getFloat( _progressParam );
            if ( progress < MathUtil::kEpsilon || context._bounds.isValid() == false )
                return point._position;
            const uint32 liftAxis = context._axisIndex;
            uint32       flipAxis = getAxisIndex( _flipAxisParam );
            if ( flipAxis == liftAxis )
                flipAxis = ( liftAxis + 1 ) % 3;

            const float32 minU  = MeshModifierUtil::getComponent( context._bounds._min, flipAxis );
            const float32 maxU  = MeshModifierUtil::getComponent( context._bounds._max, flipAxis );
            const float32 width = maxU - minU;
            if ( width < MathUtil::kEpsilon )
                return point._position;
            const float32 radius = MathUtil::max( getFloat( _radiusParam ), 0.001f );
            const float32 curl   = maxU - MathUtil::saturate( progress ) * width;
            const float32 coordU = MeshModifierUtil::getComponent( point._position, flipAxis );
            const float32 arc    = coordU - curl;
            if ( arc <= 0.0f )
                return point._position;

            const float32 height = MeshModifierUtil::getComponent( point._position, liftAxis );
            const float32 halfTurn = MathUtil::kPi * radius;
            float32       newU     = 0.0f;
            float32       newH     = 0.0f;
            if ( arc <= halfTurn )
            {
                const float32 theta = arc / radius;
                newU                = curl + ( radius - height ) * MathUtil::sin( theta );
                newH                = radius - ( radius - height ) * MathUtil::cos( theta );
            }
            else
            {
                newU = curl - ( arc - halfTurn );
                newH = 2.0f * radius - height;
            }
            float3 result = point._position;
            MeshModifierUtil::setComponent( result, flipAxis, newU );
            MeshModifierUtil::setComponent( result, liftAxis, newH );
            return result;
        }

    private:
        uint32 _progressParam;
        uint32 _radiusParam;
        uint32 _flipAxisParam;
    };

    SW_MESH_MODIFIER( PageFlipModifier, "pageflip", "Page Flip", "Deform" );
} // namespace sw::editor
