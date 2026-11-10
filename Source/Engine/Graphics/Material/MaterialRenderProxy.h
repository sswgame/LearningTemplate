/**
 * @file MaterialRenderProxy.h
 * @brief 머티리얼의 렌더 스레드 쪽 객체입니다(언리얼 `FMaterialRenderProxy` 에 해당).
 * @details 게임 스레드의 `Material` · `MaterialInstance` 는 CPU 상태(속성, 패킹한 바이트, 텍스처 요청)만 가지고, 바뀔 때마다 불변 사본
 *          `MaterialRenderData` 를 새로 만든다. 장면 빌더가 배치 · 원소마다 (프록시, 그 사본) 쌍(`MaterialRenderBinding` — 게임이 보는 헤더가 쓰도록 따로 둔다)을 스냅샷에 싣고,
 *          렌더 스레드가 받은 사본이 지난번과 다를 때만 프록시의 상수 버퍼에 올린다.
 *          한 객체를 두 스레드가 고치지 않으므로 잠금이 없다 — 사본은 불변이고, 프록시는 렌더 스레드만 고친다.
 *          갱신 기록(델타)이 아니라 상태를 싣는 이유: 그리지 않고 버려지는 패킷이 있어도 다음 패킷이 같은 상태를 다시 싣는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Material/MaterialRenderBinding.h"
#include "Engine/Graphics/RHI/RHIDeviceBuffer.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    /**
     * @struct MaterialRenderData
     * @brief 렌더 스레드가 읽는 머티리얼 값의 불변 사본입니다. 게임 스레드가 만들고 `shared_ptr<const>` 로만 건넵니다.
     */
    struct MaterialRenderData
    {
        /** @brief 셰이더 레이아웃으로 패킹한 바이트입니다(256 정렬). 비어 있으면 레이아웃 오류라 올리지 않습니다. */
        vector<uint8> _bytes;
        /** @brief g_SwMaterials 원소 stride 입니다(셰이더 리플렉션). 0 이면 레거시 MaterialCB 라 바이트 크기를 씁니다. */
        uint32 _elementStride{ 0 };
        /**
         * @brief 머티리얼 텍스처의 백엔드 SRV 인덱스(서수 순서)입니다. 네이티브 bindless 가 아닌 백엔드(DX11, GL)가 t16..t31 에 바인딩합니다.
         * @note 초기값은 칸마다 적는다 — 빠진 칸은 0(유효한 SRV 번호)이 되어 다른 텍스처를 건다.
         */
        RHIDescriptorIndex _arrTextureSRV[shaderslot::kMaterialTextureCount] = {
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
            kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        static_assert( shaderslot::kMaterialTextureCount == 16, "_arrTextureSRV 의 초기값을 칸 수만큼 적는다" );
    };

    /**
     * @class MaterialRenderProxy
     * @brief 머티리얼 하나(마스터 또는 인스턴스)의 상수 버퍼와 bindless 인덱스입니다. 렌더 스레드만 고칩니다.
     * @details 마지막 소유는 어느 스레드에서든 놓일 수 있다(게임 스레드의 Material 소멸, 패킷 반환). 핸들 반환은 디바이스가 그 프레임 뒤로 미룬다(`releaseHandle`).
     */
    class SW_API MaterialRenderProxy final : public RHIRenderResource
    {
    public:
        /** @brief 생성 키입니다. create() 만 만들 수 있습니다(제어 블록이 Engine.dll 에 있어야 모듈이 만든 머티리얼을 엔진이 마지막에 놓아도 안전하다). */
        struct CreateKey
        {
        private:
            CreateKey() = default;
            friend class MaterialRenderProxy;
        };
        /** @brief create() 전용 생성자입니다. */
        explicit MaterialRenderProxy( CreateKey );
        /** @brief Engine.dll 안에서 shared_ptr 로 만듭니다. */
        static shared_ptr<MaterialRenderProxy> create();
        /** @brief 살아 있는 디바이스면 상수 버퍼를 돌려줍니다. */
        ~MaterialRenderProxy() override;

        /**
         * @brief (렌더 스레드) @p data 를 상수 버퍼에 올립니다. 지난번과 같은 사본이고 버퍼가 이 디바이스에 있으면 아무것도 하지 않습니다.
         * @return 상수 버퍼의 bindless 인덱스입니다. 바이트가 비었거나 버퍼를 만들지 못하면 kInvalidDescriptorIndex 입니다.
         */
        RHIDescriptorIndex sync( IRHIDevice* pDevice, const shared_ptr<const MaterialRenderData>& data );

        /** @brief (RHIRenderResource) 살아 있는 디바이스에 상수 버퍼를 돌려줍니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 핸들만 버립니다. */
        void clearRHI( IRHIDevice* pDevice ) override;

    private:
        /** @brief 핸들을 비우고 다음 `sync` 가 다시 올리게 합니다. */
        void clearHandles();

    private:
        RHIDeviceBuffer                      _constant;
        uint32                               _constantByteSize; ///< `_constant` 를 만든 크기. 바이트가 이보다 커지면 다시 만든다(크기를 넘긴 update 는 버퍼 밖을 쓴다)
        RHIDescriptorIndex                   _descriptorIndex;
        shared_ptr<const MaterialRenderData> _uploaded; ///< 마지막으로 올린 사본. 같은 사본이면 다시 올리지 않는다
    };
} // namespace sw
