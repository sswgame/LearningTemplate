/**
 * @file HitDeformModifier.cpp
 * @brief 충돌점 목록(위치 · 방향 · 반경 · 세기 · 감쇠)마다 반경 안 정점을 충돌 방향으로 찌그러뜨리는 모디파이어입니다.
 * @details 실행 중 충돌 이벤트를 받아 쌓는 대신, 굽기용으로 충돌점을 매개변수 목록에 적는다(정적 충돌 적용). 좌표는 모디파이어 공간이다.
 *          항목 하나 = 위치 xyz · 방향 xyz · 반경 · 세기 · 감쇠. 이동 = 방향(단위) × 세기 × (1 - d / 반경)^감쇠지수, 감쇠지수 = 1 + 감쇠.
 *          목록이 비거나 세기가 0 이면 항등이다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 충돌점으로 찌그러뜨립니다. */
    class HitDeformModifier final : public MeshModifier
    {
    public:
        static constexpr uint32 kStride = 9;

        /** @brief Hits 표의 열 — 위치 xyz · 방향 xyz · 반경 · 세기 · 감쇠(stride 9). */
        static constexpr MeshModifierListColumn kArrColumn[] = {
            { "Position", MeshModifierListColumnKind::Vector3},
            {"Direction", MeshModifierListColumnKind::Vector3},
            {   "Radius",   MeshModifierListColumnKind::Float},
            { "Strength",   MeshModifierListColumnKind::Float},
            {  "Falloff",   MeshModifierListColumnKind::Float},
        };

        HitDeformModifier()
            : MeshModifier()
            , _hitsParam{
                  addParam( MeshModifierParamDef{ "Hits", MeshModifierParamKind::NumberList, -1000.0f, 1000.0f, float3{ 0.0f, 0.0f, 0.0f }, kStride,
                                                 "x y z dirX dirY dirZ radius strength falloff" }
                                .withColumns( kArrColumn )
                                .withLayout( "Hits", "Static hit points in modifier space that dent nearby vertices" ) )
        },
            _scaleParam{ addParam( MeshModifierParamDef{ "Scale", MeshModifierParamKind::Float, 0.0f, 10.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Response", "Multiplier applied to every hit strength" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Dents vertices within each hit radius along the hit direction (static hit list for baking)."; }

        float3 deformPoint( const MeshModifierPoint& point, const MeshModifierContext& context ) const override
        {
            (void)context;
            const vector<float32>& listNumber = getNumberList( _hitsParam );
            const size_t           count      = listNumber.size() / kStride;
            const float32          scale      = getFloat( _scaleParam );
            float3                 offset{ 0.0f, 0.0f, 0.0f };
            for ( size_t index = 0; index < count; ++index )
            {
                const float32* pItem = listNumber.data() + index * kStride;
                const float3   center{ pItem[0], pItem[1], pItem[2] };
                const float3   direction = MeshModifierUtil::normalizeOr( float3{ pItem[3], pItem[4], pItem[5] }, float3{ 0.0f, -1.0f, 0.0f } );
                const float32  radius    = MathUtil::abs( pItem[6] );
                if ( radius < MathUtil::kEpsilon )
                    continue;
                const float32 distance = ( point._position - center ).getLength();
                if ( distance >= radius )
                    continue;
                const float32 exponent = 1.0f + MathUtil::max( pItem[8], 0.0f );
                const float32 falloff  = MathUtil::pow( 1.0f - distance / radius, exponent );
                offset += direction * ( pItem[7] * falloff );
            }
            return point._position + offset * scale;
        }

    private:
        uint32 _hitsParam;
        uint32 _scaleParam;
    };

    SW_MESH_MODIFIER( HitDeformModifier, "hitdeform", "Hit Deform", "Data" );
} // namespace sw::editor
