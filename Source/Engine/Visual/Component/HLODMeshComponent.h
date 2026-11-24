/**
 * @file HLODMeshComponent.h
 * @brief 여러 정적 메시를 머티리얼 하나의 메시로 합쳐 그리는 씬 컴포넌트입니다 — 스트리밍 셀의 HLOD 대리(언리얼 HLOD 레이어 `MeshMerge`, 유니티 Static Batching 자리).
 * @details 쿠커가 셀 조각의 정적 메시 가운데 드물게 놓인 것을 머티리얼마다 이 컴포넌트 하나로 모은다(`SceneSettings::_hlodMergeMaxInstanceCount`) — 그리기가 (메시, 머티리얼)마다가 아니라 머티리얼마다 하나다.
 *          많이 놓인 메시는 인스턴스(`InstancedMeshComponent`)가 정점을 아껴 그대로 둔다(언리얼 HLOD 레이어의 Instancing).
 *          합치는 것은 부품 메시가 다 온 프레임이고, 부품마다 가장 낮은 LOD 를 쓴다(언리얼 MeshMerge 의 `LowestDetailLOD`) — 대리는 로딩 반경 밖에서만 보인다. 합친 메시는 부품 메시의 사본이라 부품 메시는 바로 놓는다.
 *          이 컴포넌트는 소스 셀 나누기(에디터 · 개발 실행)에만 있다 — 부품 메시가 바뀌면 다시 굽지 않아도 대리가 따라온다(유니티 `StaticBatchingUtility.Combine`). 쿠킹본은 쿠커가 같은 부품을 합쳐
 *          씬마다 메시 파일 하나로 굽고 대리는 그 구간을 그리는 `InstancedMeshComponent` 다(`SceneHLODMeshCooker` — 첫 로드의 합치기가 없다).
 */
#pragma once
#include "Engine/Visual/Component/InstancedMeshComponent.h"

namespace sw
{
    /** @brief 합칠 메시 하나의 자리입니다(배치 인스턴스 자리에 상대적인 위치 · 오일러 회전(라디안) · 스케일). */
    REFLECT()
    struct SW_API HLODMeshPart
    {
        REFLECT_BODY();

        PROPERTY()
        AssetRef<MeshAssetFormat> _meshID{};
        PROPERTY()
        hashed_string _sectionName{};
        PROPERTY( Units = m )
        float3 _position{};
        PROPERTY( Units = rad )
        float3 _rotation{};
        PROPERTY()
        float3 _scale{ 1.0f, 1.0f, 1.0f };
    };

    /** @class HLODMeshComponent @brief 파일 머리말 참고. 합친 메시 하나를 기반의 인스턴스 자리(보통 하나)에 그립니다. */
    REFLECT( Category = "Rendering 3D", DisplayName = "HLOD Mesh Component", Tooltip = "Merges distant static meshes of one material into a single mesh (streaming cell HLOD)", Meta = "Icon=Cube" )
    class SW_API HLODMeshComponent : public InstancedMeshComponent
    {
    public:
        REFLECT_BODY();

        HLODMeshComponent();
        ~HLODMeshComponent() override;

        /** @brief 합칠 부품들입니다. */
        const vector<HLODMeshPart>& getParts() const { return _listPart; }
        /**
         * @brief 부품 @p part 의 메시 @p mesh 가운데 가장 낮은 LOD(구간이 있으면 그 구간)를 부품 자리로 옮겨 @p inoutListVertex · @p inoutListIndex 끝에 더합니다.
         * @details 거울 스케일(행렬식이 음수)은 감김을 뒤집는다. 탄젠트는 채우지 않는다 — 합친 메시를 세울 때 다시 구한다(`Mesh::setGeometry`).
         * @return 구간 이름이 메시에 없거나 CPU 정점이 없으면 false 이고 더하지 않습니다.
         */
        [[nodiscard]] static bool appendPart( const HLODMeshPart& part, const Mesh& mesh, vector<RHIVertex>& inoutListVertex, vector<uint32>& inoutListIndex );

    protected:
        [[nodiscard]] MeshArrival acquireMesh( shared_ptr<Mesh>& outMesh ) override;
        bool                      isMeshPending() const override;

    private:
        PROPERTY( Category = "Rendering", DisplayName = "Parts", Tooltip = "Meshes merged into the one this component draws" )
        vector<HLODMeshPart> _listPart;

        vector<MeshHandle> _listPartLoad; ///< 읽는 중인 부품 메시(합친 뒤 놓는다)
    };
} // namespace sw
