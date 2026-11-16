/**
 * @file GPUMeshBatchBinding.h
 * @brief 배치 하나의 렌더 스레드 쪽 GPU 값(정점 · 인덱스 버퍼 · 풀 시작 · 변형 풀 시작 · 머티리얼 버퍼 · CB · 텍스처 슬롯)입니다.
 * @details 예전에는 이 칸들이 게임 스레드 배치(`GPUMeshBatch`) 안에 있어 렌더 스레드가 받은 사본에 덧썼다 — 그래서 배치 목록을 공유하지 못하고
 *          패킷마다 통째로 베꼈다(배치마다 shared_ptr 원자 연산). 이제 배치 목록은 불변 공유 값(`GPUSceneSnapshot`)이고, 렌더 스레드가 쓰는 칸은
 *          `GPUScene` 이 배치와 같은 번호로 따로 든다. 게임 스레드는 이 형식을 보지 않는다 — 렌더 쪽 값을 게임 쪽이 덮을 길이 형식으로 없다.
 *          언리얼은 `FMeshBatch`(게임 쪽 서술)와 `FMeshDrawCommand`(렌더 쪽 바인딩)를 나눈다(기억에 의존).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    /**
     * @struct GPUMeshBatchBinding
     * @brief 배치 하나의 렌더 쪽 값입니다. `GPUScene` 이 스냅샷을 받을 때마다 기본값으로 다시 깔고 upload 가 채운다(예전 사본과 같은 수명).
     */
    struct GPUMeshBatchBinding
    {
        RHIBufferHandle _vertexBuffer{ 0 };    ///< [위치 구간][속성 구간] 정점 버퍼(RHIMeshVertexStreams) — 형식은 배치 메시의 `getVertexFormat()`
        uint32          _attributeOffset{ 0 }; ///< `_vertexBuffer` 에서 속성 구간이 시작하는 바이트(슬롯 2 를 여기서 건다)
        RHIBufferHandle _indexBuffer{ 0 };
        /**
         * @brief 정점 풀 안에서 이 메시의 시작 위치(정점 단위)입니다. 간접 인자의 baseVertex 가 됩니다. 풀 밖의 메시는 0 이며 자기 정점 · 인덱스 버퍼를 씁니다.
         * @details upload() 에서 채운다. 같은 풀 버퍼를 쓰는 배치들은 정점 · 인덱스 버퍼를 다시 바인딩하지 않고 멀티 드로우로 묶인다.
         */
        uint32 _baseVertex{ 0 };
        /// @brief 인덱스 버퍼 안에서 이 배치의 첫 인덱스입니다(풀이면 메시의 인덱스 풀 시작 + 구간 첫 인덱스). 간접 인자의 startIndex 가 됩니다.
        uint32 _firstIndex{ 0 };
        /** @brief 모프 풀에서 이 배치 메시의 시작 오프셋(정점 단위)입니다. 0xFFFFFFFF 이면 모프를 하지 않습니다(`GPUScene::assignMorphBases`). 배치 표 `GPUBatchInfo` 에 실린다. */
        uint32 _morphVertexBase{ 0xFFFFFFFFu };
        /** @brief 정점 애니메이션(VAT) 테이블에서 이 배치 메시의 첫 원소입니다. 0xFFFFFFFF 이면 VAT 가 없습니다(`GPUScene::assignVertexAnimationBases`). */
        uint32 _vertexAnimationBase{ 0xFFFFFFFFu };
        /**
         * @brief 변형(모프 · 스킨 · VAT)이 필요한 메시인데 풀이 받지 못했습니다(상한 초과 · 버퍼 생성 실패). 이 배치는 그리지 않습니다.
         * @details 바인드 · 레스트 포즈로 대신 그리지 않는다 — 풀이 이유를 오류로 남겼다(`FramePassExecutor::shouldDrawBatchInPass` 가 거른다).
         */
        uint8 _bDeformRefused{ SW_FALSE };
        /** @brief 배치 머리 원소(`GPUMeshBatch::_materialIndex`)의 머티리얼 상수 버퍼 bindless 인덱스입니다(`MaterialRenderProxy::sync`). */
        RHIDescriptorIndex _materialCb = kInvalidDescriptorIndex;
        /** @brief 그룹의 GPU 구조 버퍼(g_SwMaterials, t5)와 SRV 인덱스입니다. 인스턴스의 _materialIndex 가 이 버퍼의 원소를 고른다. */
        RHIBufferHandle    _materialBuffer{ 0 };
        RHIDescriptorIndex _materialSRV = kInvalidDescriptorIndex;
        /** @brief 그룹 버퍼의 원소 수입니다. 드로우 루트 상수 g_SwMaterialCount 로 넘겨 셰이더가 인덱스를 클램프하게 합니다. */
        uint32 _materialCount{ 0 };
        /**
         * @brief 머티리얼 텍스처의 백엔드 SRV 인덱스(서수 순서)입니다. 네이티브 bindless 가 아닌 백엔드(DX11 · GL — t16..t31 에 직접 건다)에서만 씁니다.
         * @note 초기값은 칸마다 적는다 — 빠진 칸은 0(유효한 SRV 번호)이 되어 다른 텍스처를 건다. 칸 수가 바뀌면 아래 static_assert 가 먼저 멈춘다.
         */
        RHIDescriptorIndex _arrMaterialTexSRV[shaderslot::kMaterialTextureCount] = {
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        static_assert( shaderslot::kMaterialTextureCount == 16, "_arrMaterialTexSRV 의 초기값을 칸 수만큼 적는다" );

        /** @brief 정점 · 인덱스 버퍼가 있어 그릴 수 있으면 true 입니다(둘은 짝이다 — 풀의 둘, 아니면 메시의 둘). */
        bool hasGeometry() const { return _vertexBuffer != 0 && _indexBuffer != 0; }

        /**
         * @brief 두 배치가 머티리얼 쪽에서 멀티 드로우 하나로 묶일 수 있는지 판단합니다. 버퍼, SRV, 원소 수, 텍스처가 같아야 하고, 머티리얼 CB 는
         *        셰이더가 그 슬롯을 바인딩할 때만(@p bShaderBindsMaterialCb) 비교합니다.
         * @details GPUScene 경로의 셰이더는 머티리얼을 구조 버퍼(`g_SwMaterials`, t5)에서 인스턴스의 `_materialIndex` 로 읽으므로 CB 슬롯이 없다.
         *          그런 셰이더에서 `_materialCb` 를 키에 넣으면 깊이순으로 섞인 투명 배치들이 이미지에 영향이 없는 값 때문에 하나씩 따로 그려진다
         *          (큐브 8000개와 도형 8종 씬에서 배치 1795개가 드로우 1206회였고, 이 값을 빼면 3회이면서 화면은 같다).
         */
        static bool canShareMaterialBinding( const GPUMeshBatchBinding& head, const GPUMeshBatchBinding& other, bool bShaderBindsMaterialCb )
        {
            const bool bDifferentMaterialBinding = other._materialBuffer != head._materialBuffer || other._materialSRV != head._materialSRV || other._materialCount != head._materialCount;
            if ( bDifferentMaterialBinding )
                return false;
            if ( bShaderBindsMaterialCb && other._materialCb != head._materialCb )
                return false;
            for ( uint32 texIndex = 0; texIndex < shaderslot::kMaterialTextureCount; ++texIndex )
            {
                if ( other._arrMaterialTexSRV[texIndex] != head._arrMaterialTexSRV[texIndex] )
                    return false;
            }
            return true;
        }
    };
} // namespace sw
