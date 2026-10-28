/**
 * @file PaintModifier.cpp
 * @brief 구형 브러시 목록으로 정점 색이나 선택 가중치를 칠하는 모디파이어입니다. 위치는 바꾸지 않습니다.
 * @details 브러시 하나 = x y z 반경 세기 감쇠(모디파이어 공간). 정점까지 거리 d < 반경이면 덮기 a = saturate(|세기| · f),
 *          f = lerp(1, smoothstep(1 - d / 반경), saturate(감쇠)). 브러시는 목록 순서대로 쌓인다.
 *          대상 0 = 정점 색: rgb 를 `Color` 쪽으로 a 만큼 섞는다(세기 < 0 이면 흰색 쪽). 색 채널이 없으면 흰색으로 만든 뒤 칠한다.
 *          대상 1 = 선택 가중치: 세기 ≥ 0 이면 1 쪽, < 0 이면 0 쪽으로 a 만큼 섞는다. 가중치 채널이 없으면 0 으로 만든 뒤 칠한다(칠한 곳만 선택).
 *          브러시 목록이 비면 아무 채널도 만들지 않는다(항등). 전체 채널을 다루므로 `applyToMesh` 를 재정의하고 GPU 미리보기는 없다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct PaintModifierInternal
        {
            static constexpr uint32 kBrushStride = 6;

            static float32 lerp( float32 from, float32 to, float32 t ) { return from + ( to - from ) * t; }

            /** @brief 브러시 하나가 점을 덮는 비율 [0,1] 입니다. 반경이 0 이하면 0 입니다. */
            static float32 computeCoverage( const float32* pBrush, const float3& point )
            {
                const float32 radius = pBrush[3];
                if ( radius <= MathUtil::kEpsilon )
                    return 0.0f;
                const float32 distance = ( point - float3{ pBrush[0], pBrush[1], pBrush[2] } ).getLength();
                if ( distance >= radius )
                    return 0.0f;
                const float32 inner   = 1.0f - distance / radius;
                const float32 smooth  = inner * inner * ( 3.0f - 2.0f * inner );
                const float32 falloff = lerp( 1.0f, smooth, MathUtil::saturate( pBrush[5] ) );
                return MathUtil::saturate( MathUtil::abs( pBrush[4] ) * falloff );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 정점 색 · 선택 가중치 칠하기입니다. */
    class PaintModifier final : public MeshModifier
    {
    public:
        /** @brief Brushes 표의 열 — 위치 xyz · 반경 · 세기 · 감쇠(stride 6). */
        static constexpr MeshModifierListColumn kArrColumn[] = {
            {"Position", MeshModifierListColumnKind::Vector3},
            {  "Radius",   MeshModifierListColumnKind::Float},
            {"Strength",   MeshModifierListColumnKind::Float},
            { "Falloff",   MeshModifierListColumnKind::Float},
        };

        PaintModifier()
            : MeshModifier()
            , _targetParam{
                  addParam( MeshModifierParamDef{ "Target", MeshModifierParamKind::Int, 0.0f, 1.0f, float3{ 0.0f, 0.0f, 0.0f } }
                                .withLayout( nullptr, "0 = paint vertex color, 1 = paint vertex weight" ) )
        },
            _colorParam{ addParam( MeshModifierParamDef{ "Color", MeshModifierParamKind::Vector3, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( nullptr, "Color painted by the brushes (Target 0)" ) ) },
            _brushesParam{ addParam( MeshModifierParamDef{ "Brushes", MeshModifierParamKind::NumberList, -100000.0f, 100000.0f, float3{ 0.0f, 0.0f, 0.0f }, PaintModifierInternal::kBrushStride, "x y z radius strength falloff" }.withColumns( kArrColumn ) ) }
        {
        }

        const utf8* getDescription() const override { return "Paints vertex color or weight with spherical brushes."; }

        /**
         * @brief 위치 · 법선을 안 바꾸는 항등 기록이다. 색 대상은 세션이 GPU 메시의 정점 스트림 색을 CPU 결과로 덮고, 가중치 대상의 결과는 뒤따르는 모디파이어의 기록이
         *        싣는 정점별 가중치 표(입력 가중치 수집)로 간다.
         */
        bool supportsGPU() const override { return true; }
        bool writesGPUVertexColor() const override { return getInt( _targetParam ) == 0; }

        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            (void)context;
            const vector<float32>& listNumber = getNumberList( _brushesParam );
            const size_t           brushCount = listNumber.size() / PaintModifierInternal::kBrushStride;
            if ( brushCount == 0 )
                return;
            const size_t vertexCount = inoutMesh.getVertexCount();
            const bool   bWeight     = getInt( _targetParam ) == 1;
            if ( bWeight && inoutMesh.hasWeight() == false )
                inoutMesh._listWeight.assign( vertexCount, 0.0f );
            if ( bWeight == false && inoutMesh.hasColor() == false )
                inoutMesh._listColor.assign( vertexCount, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );

            const float3& paintColor = getVector3( _colorParam );
            for ( size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
            {
                const float3& point = inoutMesh._listPosition[vertexIndex];
                for ( size_t brushIndex = 0; brushIndex < brushCount; ++brushIndex )
                {
                    const float32* pBrush   = listNumber.data() + brushIndex * PaintModifierInternal::kBrushStride;
                    const float32  coverage = PaintModifierInternal::computeCoverage( pBrush, point );
                    if ( coverage <= 0.0f )
                        continue;
                    const bool bErase = pBrush[4] < 0.0f;
                    if ( bWeight )
                    {
                        float32& weight = inoutMesh._listWeight[vertexIndex];
                        weight          = MathUtil::saturate( PaintModifierInternal::lerp( weight, bErase ? 0.0f : 1.0f, coverage ) );
                    }
                    else
                    {
                        float4&      color  = inoutMesh._listColor[vertexIndex];
                        const float3 target = bErase ? float3{ 1.0f, 1.0f, 1.0f } : paintColor;
                        color._x            = PaintModifierInternal::lerp( color._x, target._x, coverage );
                        color._y            = PaintModifierInternal::lerp( color._y, target._y, coverage );
                        color._z            = PaintModifierInternal::lerp( color._z, target._z, coverage );
                    }
                }
            }
        }

    private:
        uint32 _targetParam;
        uint32 _colorParam;
        uint32 _brushesParam;
    };

    SW_MESH_MODIFIER( PaintModifier, "paint", "Paint", "Selection" );
} // namespace sw::editor
