/**
 * @file ConformMultiModifier.cpp
 * @brief 여러 대상 메시(최대 kTargetCount 개)의 표면 가운데 정점마다 가장 가까운(또는 반직선이 먼저 닿는) 표면으로 붙이는 모디파이어입니다.
 * @details 참조 목록 매개변수 종류가 없어 참조 칸 넷을 둔다. 빈 칸 · 못 찾은 대상은 건너뛰고, 모두 비면 항등이다.
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
        static constexpr uint32 kTargetCount = 4;

        ConformMultiModifier()
            : MeshModifier()
            , _arrTargetParam{ addParam( MeshModifierParamDef{ "Target 1", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh object" }.withLayout( "Targets", "Mesh object to conform onto; empty slots are skipped" ).withRequiredComponent( "MeshComponent" ) ),
                               addParam( MeshModifierParamDef{ "Target 2", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh object" }.withLayout( "Targets", "Mesh object to conform onto; empty slots are skipped" ).withRequiredComponent( "MeshComponent" ) ),
                               addParam( MeshModifierParamDef{ "Target 3", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh object" }.withLayout( "Targets", "Mesh object to conform onto; empty slots are skipped" ).withRequiredComponent( "MeshComponent" ) ),
                               addParam( MeshModifierParamDef{ "Target 4", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh object" }.withLayout( "Targets", "Mesh object to conform onto; empty slots are skipped" ).withRequiredComponent( "MeshComponent" ) ) }
            , _modeParam{ addParam( MeshModifierParamDef{ "Mode", MeshModifierParamKind::Int, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "0 closest, 1 axis, 2 normal" }.withLayout( "Projection", "Projection mode: 0 closest point, 1 along the negative main axis, 2 along the negative normal" ) ) }
            , _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Distance kept above the target surface", "m" ) ) }
            , _maxDistanceParam{ addParam( MeshModifierParamDef{ "Max Distance", MeshModifierParamKind::Float, 0.0f, 100000.0f, float3{ 1000.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Points farther than this from the target are left in place", "m" ) ) }
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Blend between the original shape (0) and the conformed shape (1)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Projects points onto the nearest surface among up to four mesh objects."; }

        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            vector<float3> listTriangle;
            for ( const uint32 targetParam : _arrTargetParam )
            {
                // 못 찾은 대상은 건너뛴다 — 남은 대상만으로 맞춘다
                static_cast<void>( PathModifierUtil::tryAppendTriangles( context, getText( targetParam ), listTriangle ) );
            }
            PathModifierUtil::conformMesh( inoutMesh, context, listTriangle, static_cast<PathModifierUtil::ConformMode>( getInt( _modeParam ) ),
                                           getFloat( _offsetParam ), getFloat( _maxDistanceParam ), getFloat( _amountParam ) );
        }

    private:
        uint32 _arrTargetParam[kTargetCount];
        uint32 _modeParam;
        uint32 _offsetParam;
        uint32 _maxDistanceParam;
        uint32 _amountParam;
    };

    SW_MESH_MODIFIER( ConformMultiModifier, "conformmulti", "Conform Multi", "Surface" );
} // namespace sw::editor
