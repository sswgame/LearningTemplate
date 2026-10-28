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
            , _targetParam{
                  addParam( MeshModifierParamDef{ "Target", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Mesh object" }
                                .withLayout( "Target", "Mesh object to conform onto" )
                                .withRequiredComponent( "MeshComponent" ) )
        },
            _modeParam{ addParam( MeshModifierParamDef{ "Mode", MeshModifierParamKind::Int, 0.0f, 2.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "0 closest, 1 axis, 2 normal" }.withLayout( "Projection", "Projection mode: 0 closest point, 1 along the negative main axis, 2 along the negative normal" ) ) },
            _offsetParam{ addParam( MeshModifierParamDef{ "Offset", MeshModifierParamKind::Float, -100.0f, 100.0f, float3{ 0.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Distance kept above the target surface", "m" ) ) },
            _maxDistanceParam{ addParam( MeshModifierParamDef{ "Max Distance", MeshModifierParamKind::Float, 0.0f, 100000.0f, float3{ 1000.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Points farther than this from the target are left in place", "m" ) ) },
            _amountParam{ addParam( MeshModifierParamDef{ "Amount", MeshModifierParamKind::Float, 0.0f, 1.0f, float3{ 1.0f, 0.0f, 0.0f } }.withLayout( "Projection", "Blend between the original shape (0) and the conformed shape (1)" ) ) }
        {
        }

        const utf8* getDescription() const override { return "Projects points onto the surface of another mesh object."; }

        bool supportsGPU() const override { return true; }

        /**
         * @brief 대상 삼각형을 모디파이어 공간으로 옮겨 BVH 로 지어 싣는다: float4(얻음, 삼각형 수, 노드 수) · 노드 표 · 삼각형 표.
         * @details 셰이더가 스택으로 BVH 를 훑는다(CPU `ConformSurface` 와 같은 질의: 최근접점 · 광선). 같은 삼각형이면 BVH 를 다시 짓지 않는다. 삼각형이 `kGPUConformTriangleLimit` 를 넘으면
         *          프로그램을 만들지 못하고(false) 미리보기가 CPU 경로로 돌아간다 - 거짓으로 일부만 싣지 않는다.
         */
        bool packGPUInputs( const MeshModifierContext& context, vector<float4>& outListValue ) const override
        {
            vector<float3> listTriangle;
            static_cast<void>( PathModifierUtil::tryAppendTriangles( context, getText( _targetParam ), listTriangle ) );
            return PathModifierUtil::packGPUConformTable( listTriangle, _gpuTable, outListValue );
        }

        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const override
        {
            vector<float3> listTriangle;
            if ( PathModifierUtil::tryAppendTriangles( context, getText( _targetParam ), listTriangle ) == false )
                return;
            PathModifierUtil::conformMesh( inoutMesh, context, listTriangle, static_cast<PathModifierUtil::ConformMode>( getInt( _modeParam ) ),
                                           getFloat( _offsetParam ), getFloat( _maxDistanceParam ), getFloat( _amountParam ), _surface );
        }

    private:
        uint32                  _targetParam;
        uint32                  _modeParam;
        uint32                  _offsetParam;
        uint32                  _maxDistanceParam;
        uint32                  _amountParam;
        mutable ConformGPUTable _gpuTable; ///< GPU 프로그램에 싣는 BVH 캐시(같은 삼각형이면 다시 짓지 않는다)
        mutable ConformSurface  _surface;  ///< 대상 삼각형이 안 바뀌면 BVH 를 다시 만들지 않는다(같은 인스턴스를 두 스레드에서 동시에 적용하지 않는다)
    };

    SW_MESH_MODIFIER( ConformModifier, "conform", "Conform", "Surface" );
} // namespace sw::editor
