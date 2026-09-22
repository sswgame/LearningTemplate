/**
 * @file MeshInstanceBatch.h
 * @brief 씬 컴포넌트 없이 같은 메시·머티리얼의 인스턴스 N 개를 드는 렌더 프리미티브 — 언리얼 InstancedStaticMesh 의 자리.
 * @details 게임플레이 객체는 `GameObject` + `SceneComponent` 로 두고, 수천 개가 매 프레임 움직이는 것은 이 배치로 다룬다.
 *          항목은 월드 행렬 하나와 바운드·시드뿐이라 플러시도 배치 쓰기도 지나지 않는다 — `PrimitiveRegistry` 에 항목마다
 *          프리미티브 번호로 등록되어 `GpuSceneBuilder` 가 메시 컴포넌트와 **같은 경로**로 모은다(부분 수집 · 배치 병합 ·
 *          더티 구간 · 투명 정렬 전부 그대로). 항목 수는 만들 때 정해진다(언리얼 ISM 도 추가·제거가 재구성이다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class Material;
    class MaterialInstance;
    class Mesh;
    class PrimitiveRegistry;

    class SW_API MeshInstanceBatch
    {
    public:
        /** @brief 인스턴스 하나 — 렌더에 필요한 것만 든다. */
        struct Entry
        {
            float4x4 _world{ float4x4::Identity };
            float32  _boundsRadius{ 0.866f }; ///< 단위 큐브의 반지름 — MeshComponent 의 기본값과 같다
            uint32   _spinSeed{ 0 };          ///< GPU 회전 시드 (0 = 없음). instanceanim.hlsl 이 해시한다
        };

        /**
         * @brief 메시 하나 · 머티리얼 하나 · 항목 count 개. 머티리얼이 nullptr 이면 씬 기본 머티리얼로 그려진다.
         * @param pMaterial 소유하지 않는다 — MeshComponent 와 같은 규칙이다(빌더가 스냅샷에 소유를 싣는다).
         */
        MeshInstanceBatch( shared_ptr<Mesh> mesh, Material* pMaterial, shared_ptr<MaterialInstance> instance, uint32 count );
        /** @brief 등록되어 있으면 등록부에서 빠진다. */
        ~MeshInstanceBatch();

        MeshInstanceBatch( const MeshInstanceBatch& )            = delete;
        MeshInstanceBatch& operator=( const MeshInstanceBatch& ) = delete;

        /** @brief 항목 수. */
        uint32 getCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief 항목 하나 — 빌더가 후보를 채울 때 읽는다. */
        const Entry& getEntry( uint32 index ) const { return _listEntry[index]; }

        /** @brief 항목의 월드 행렬을 적고 더티로 표시합니다. 프레임마다 움직이는 자리라 비교하지 않는다. */
        void setWorld( uint32 index, const float4x4& world );
        /** @brief 항목의 바운드 반지름을 적고 더티로 표시합니다. */
        void setBoundsRadius( uint32 index, float32 radius );
        /** @brief 항목의 GPU 회전 시드를 적고 더티로 표시합니다. */
        void setSpinSeed( uint32 index, uint32 seed );
        /** @brief 배치 전체를 보이거나 숨깁니다 — 항목 전부가 더티가 된다. */
        void setVisible( bool bVisible );
        /** @brief 보이는가. */
        bool isVisible() const { return _bVisible != SW_FALSE; }

        /** @brief 메시 (날 포인터). */
        Mesh* getRawMesh() const { return _mesh.get(); }
        /** @brief 메시 (소유). */
        const shared_ptr<Mesh>& getMesh() const { return _mesh; }
        /** @brief 머티리얼 (소유하지 않는다). nullptr 이면 씬 기본. */
        Material* getMaterial() const { return _pMaterial; }
        /** @brief 머티리얼 인스턴스 (날 포인터). */
        MaterialInstance* getRawMaterialInstance() const { return _instance.get(); }
        /** @brief 머티리얼 인스턴스 (소유). */
        const shared_ptr<MaterialInstance>& getMaterialInstance() const { return _instance; }
        /** @brief 등록부에 들어 있으면 true. */
        bool isRegistered() const { return _pRegistry != nullptr; }

    private:
        friend class PrimitiveRegistry;

        /** @brief 항목 하나를 등록부에 더티로 알립니다. 등록 전이면 아무것도 하지 않는다. */
        void markDirty( uint32 index );

        vector<Entry>                _listEntry;
        shared_ptr<Mesh>             _mesh;
        Material*                    _pMaterial;
        shared_ptr<MaterialInstance> _instance;
        PrimitiveRegistry*           _pRegistry;  ///< 등록된 등록부. 등록부가 먼저 죽으면 등록부가 비운다
        uint32                       _firstEntry; ///< 등록부의 인스턴스 항목 목록에서 첫 항목 자리 — 등록부가 적는다
        uint8                        _bVisible;
    };
} // namespace sw
