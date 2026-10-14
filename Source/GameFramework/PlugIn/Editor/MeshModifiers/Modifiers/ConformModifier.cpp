/**
 * @file ConformModifier.cpp
 * @brief 정점을 대상 메시 표면으로 투영해 붙이는 모디파이어입니다.
 * @details 방식: 0 = 가장 가까운 표면 점, 1 = 주축 음의 방향 반직선, 2 = 정점 법선 음의 방향 반직선. 찾은 점에서 표면 법선(원래 점 쪽)으로
 *          오프셋만큼 띄우고, 최대 거리보다 멀면 그 정점은 그대로 둔다. 세기 × 선택 가중치로 섞는다. 대상 참조가 비면 항등이다.
 *          정점 전체를 한 번에 보려고 `applyToMesh` 를 재정의하고, 법선은 면 법선으로 다시 만든다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    /** @brief 표면 맞추기입니다. */
    class ConformModifier final : public MeshModifier
    {
    public:
        ConformModifier()
            : MeshModifier()
            , _targetParam{ addParam( MeshModifierParamDef{ "Target", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh object" } ) }
            , _modeParam{ addParam( MeshModifierParamDef{ "Mode", MeshModifierParamKind::Int, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "0 closest, 1 axis, 2 normal" } ) }
            , _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } } ) }
            , _maxDistanceParam{ addParam( MeshModifierParamDef{ "Max Distance", MeshModifierParamKind::Float, 0.0f, 100000.0f, float3{ 1000.0f, 0.0f, 0.0f } } ) }
            , _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } } ) }
        {
        }

        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            vector<float3> listTriangle;
            if ( PathModifierUtil::tryAppendTriangles( context, getText( _targetParam ), listTriangle ) == false )
                return;
            PathModifierUtil::conformMesh( inoutMesh, context, listTriangle, static_cast<PathModifierUtil::ConformMode>( getInt( _modeParam ) ),
                                           getFloat( _offsetParam ), getFloat( _maxDistanceParam ), getFloat( _amountParam ) );
        }

    private:
        uint32 _targetParam;
        uint32 _modeParam;
        uint32 _offsetParam;
        uint32 _maxDistanceParam;
        uint32 _amountParam;
    };

    SW_MESH_MODIFIER( ConformModifier, "conform", "Conform", "Surface" );
} // namespace sw::editor
