/**
 * @file ConformMultiModifier.cpp
 * @brief 여러 대상 메시(참조 목록 "Targets", 최대 kMaxTargetCount 개)의 표면 가운데 정점마다 가장 가까운(또는 반직선이 먼저 닿는) 표면으로 붙이는 모디파이어입니다.
 * @details 빈 이름 · 못 찾은 대상은 건너뛰고, 목록이 비거나 모두 못 찾으면 항등이다.
 *          투영 규칙(방식 · 오프셋 · 최대 거리 · 세기)은 Conform 과 같다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 여러 표면 맞추기입니다. */
    class ConformMultiModifier final : public MeshModifier
    {
    public:
        static constexpr float32 kMaxTargetCount = 16.0f; ///< "Targets" 목록의 항목 수 상한

        ConformMultiModifier()
            : MeshModifier()
            , _targetsParam{
                  addParam( MeshModifierParamDef{ "Targets", MeshModifierParamKind::ReferenceList, 0.0f, kMaxTargetCount, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh objects" }
                                .withLayout( "Targets", "Mesh objects to conform onto; the nearest surface among all of them wins, and unresolved names are skipped" )
                                .withRequiredComponent( "MeshComponent" ) )
        },
            _modeParam{ addParam( MeshModifierParamDef{ "Mode", MeshModifierParamKind::Int, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "0 closest, 1 axis, 2 normal" }.withLayout( "Projection", "Projection mode: 0 closest point, 1 along the negative main axis, 2 along the negative normal" ) ) },
            _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Distance kept above the target surface", "m" ) ) },
            _maxDistanceParam{ addParam( MeshModifierParamDef{ "Max Distance", MeshModifierParamKind::Float, 0.0f, 100000.0f, float3{ 1000.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Points farther than this from the target are left in place", "m" ) ) },
            _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Blend between the original shape (0) and the conformed shape (1)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Projects points onto the nearest surface among several mesh objects."; }

        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            vector<float3> listTriangle;
            for ( const string& target : getParamValue( _targetsParam )._listText )
            {
                // 못 찾은 대상은 건너뛴다 — 남은 대상만으로 맞춘다
                static_cast<void>( PathModifierUtil::tryAppendTriangles( context, target, listTriangle ) );
            }
            PathModifierUtil::conformMesh( inoutMesh, context, listTriangle, static_cast<PathModifierUtil::ConformMode>( getInt( _modeParam ) ),
                                           getFloat( _offsetParam ), getFloat( _maxDistanceParam ), getFloat( _amountParam ), _surface );
        }

    private:
        uint32                 _targetsParam;
        uint32                 _modeParam;
        uint32                 _offsetParam;
        uint32                 _maxDistanceParam;
        uint32                 _amountParam;
        mutable ConformSurface _surface; ///< 대상 삼각형이 안 바뀌면 BVH 를 다시 만들지 않는다(같은 인스턴스를 두 스레드에서 동시에 적용하지 않는다)
    };

    SW_MESH_MODIFIER( ConformMultiModifier, "conformmulti", "Conform Multi", "Surface" );
} // namespace sw::editor
