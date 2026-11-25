#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"

namespace sw
{
    struct VulkanDescriptorPoolSet;
    struct VulkanRecordingState;
    struct VulkanSlotState;

    class VulkanRHIDevice;

    class VulkanRHICommandContext : public IRHICommandContext
    {
    public:
        /**
         * @brief 디바이스의 기록 상태와 버퍼를 쓰는 프레임 스트림 컨텍스트를 만듭니다.
         * @details 백버퍼 패스와 Present 처럼 "디바이스가 직접 여는 버퍼"(프레임 세그먼트)에 기록하는 경로용입니다.
         */
        explicit VulkanRHICommandContext( VulkanRHIDevice* pDevice );
        /**
         * @brief 지정한 커맨드 버퍼와 기록 상태에 기록하는 컨텍스트를 만듭니다.
         * @details `VulkanRHICommandList` 처럼 자기 버퍼와 상태를 소유하는 쪽이 씁니다. 여러 리스트가
         *          동시에 기록해도 서로의 바인딩 캐시를 건드리지 않습니다.
         */
        VulkanRHICommandContext( VulkanRHIDevice* pDevice, VkCommandBuffer targetBuffer, VulkanRecordingState* pState,
                                 VulkanDescriptorPoolSet* pDescriptorPoolSet );
        /** @brief 이 컨텍스트가 브레드크럼을 쓰는 칸의 시작입니다(비동기 컴퓨트 리스트는 제 칸 — `VulkanRHIDevice::kComputeBreadcrumbSlotBase`). */
        void setBreadcrumbSlotBase( uint32 slotBase ) { _breadcrumbSlotBase = slotBase; }
        ~VulkanRHICommandContext() override = default;

        void blitTextureNative( RHITextureHandle src, RHITextureHandle dst ) override;
        void prepareTextureForShaderReadNative( RHITextureHandle texture ) override;
        void prepareTextureForRenderTargetNative( RHITextureHandle texture ) override;
        void prepareTextureForUnorderedAccessNative( RHITextureHandle texture ) override;
        /** @brief 텍스처 전이를 `vkCmdPipelineBarrier` 한 번으로 냅니다(`VulkanRHIDevice::transitionTextureLayouts`). 단일 `prepareTextureFor*Native` 도 원소 하나로 이 길을 지난다. */
        void transitionTexturesNative( const RHITextureTransition* pTransition, uint32 count ) override;
        void drawNative( uint32 vertexCount, uint32 startVertex = 0 ) override;
        void drawInstancedNative( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) override;
        /** @brief 버퍼의 이번 프레임 슬롯에 씁니다. 이 백엔드의 커맨드 스트림은 버퍼 버전을 갖지 않습니다. */
        void                     updateConstantBufferNative( RHIBufferHandle buffer, const void* pData, uint32 size ) override;
        void                     dispatchComputeNative( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) override;
        void                     setViewportNative( const RHIViewport& viewport ) override;
        void                     setScissorRectNative( const RHIScissorRect& rect ) override;
        void                     drawIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                                                     RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) override;
        void                     drawIndexedIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1, RHIBufferHandle countBuffer = 0,
                                                            uint32 countBufferOffset = 0 ) override;
        void                     drawIndexedInstancedNative( uint32 indexCount, uint32 instanceCount, uint32 startIndex = 0, int32 baseVertex = 0, uint32 startInstance = 0 ) override;
        void                     dispatchIndirectNative( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) override;
        void                     beginEventMarker( const utf8* pName ) override;
        void                     writeBreadcrumb( uint32 markerID ) override;
        void                     endEventMarker() override;
        IRHIDevice&              getContextDevice() const override;
        RHIRenderPassBeginResult beginRenderPassNative( const RHIRenderPassBeginInfo& beginInfo, RHIRenderPassExtent& outTargetExtent ) override;
        void                     endRenderPassNative() override;
        void                     transitionBufferNative( RHIBufferHandle buffer, RHIBufferState newState ) override;
        void                     transitionBuffersNative( const RHIBufferTransition* pTransition, uint32 count ) override;
        void                     copyBufferToReadbackNative( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) override;
        uint64                   findBufferBytes( RHIBufferHandle buffer ) const override;
        void                     copyBufferRegionNative( RHIBufferHandle destination, uint64 destinationOffset, RHIBufferHandle source, uint64 sourceOffset, uint64 sizeBytes ) override;
        void                     uavBarrierNative( RHIBufferHandle buffer ) override;
        void                     uavBarrierTextureNative( RHITextureHandle texture ) override;
        void                     beginTextureAliasNative( RHITextureHandle texture ) override;
        /** @brief 마지막으로 연 이벤트 마커입니다(없으면 빈 문자열). 기록이 끝난 뒤 제출하는 스레드가 읽습니다. */
        const utf8* getLastEventMarker() const { return _lastEventMarker.c_str(); }

        /** @brief 새 커맨드 버퍼에 기록을 시작합니다 — 걸린 것이 없다(`VulkanRecordingState` 를 되돌리는 자리와 짝이다). */
        void beginRecording() { resetBindings(); }
        /** @brief 기록 대상 버퍼와 풀 그룹을 교체합니다(소유자가 버퍼와 풀 쌍을 바꿀 때). */
        void rebindCommandBuffer( VkCommandBuffer targetBuffer, VulkanDescriptorPoolSet* pDescriptorPoolSet )
        {
            _targetBuffer       = targetBuffer;
            _pDescriptorPoolSet = pDescriptorPoolSet;
        }

    private:
        /**
         * @brief 바뀐 셰이더 슬롯을 슬롯 세트(set 0)로 확정해 그 바인드 포인트에 바인딩하고 바뀐 루트 상수를 푸시합니다. 텍스처 세트(set 1)는 커맨드 버퍼마다 한 번 두 바인드 포인트에 바인딩합니다.
         * @details 언리얼 Vulkan RHI 처럼 드로우와 디스패치 직전에 부릅니다. 상태가 그대로면 아무것도 하지 않습니다.
         */
        void flushBindings( RHIBindPoint point );
        /** @brief 공용 표의 슬롯 하나를 버퍼 구간으로 풀어 세트 원소(@p bindingIndex = 종류별 시프트 + 레지스터)에 적습니다. 뗀 칸 · 등록되지 않은 번호는 비운다(뒤는 알린다). */
        void resolveSlot( VulkanSlotState& state, RHIShaderSlotKind kind, uint32 slot, RHIDescriptorIndex index ) const;
        /**
         * @brief 드로우 하나의 준비입니다 — 파이프라인 · 슬롯 세트 · 루트 상수 · 정점(@p bIndexed 면 인덱스도). 모든 드로우 진입점이 부릅니다.
         * @return 걸 수 없으면(커맨드 버퍼 없음 · 빈 정점 슬롯 · 부서진 버퍼) false 이고 드로우를 내지 않습니다. 등록된 PSO 가 없으면 오류로 멈춥니다.
         */
        [[nodiscard]] bool prepareDraw( bool bIndexed );
        /** @brief 디스패치 하나의 준비입니다 — 컴퓨트 파이프라인 · 슬롯 세트 · 루트 상수. 등록된 컴퓨트 PSO 가 없으면 오류로 멈춥니다(드로우의 그래픽스 PSO 와 같은 계약 — 네 백엔드가 같습니다). */
        void prepareDispatch();

        /** @brief 파이프라인이 선언한 바인딩(@p layout — 셰이더가 읽는 슬롯)에만 정점 버퍼를 겁니다. 읽는 슬롯이 비었거나 건 버퍼가 부서졌으면 알리고 false 이며 드로우를 버립니다. */
        [[nodiscard]] bool bindMeshVertexBufferForDraw( const RHIVertexLayout& layout );

        /** @brief 실제로 기록할 커맨드 버퍼입니다(지정된 것이 있으면 그것, 없으면 디바이스의 현재 버퍼). */
        VkCommandBuffer commandBuffer() const;

    private:
        VulkanRHIDevice* _pDevice;
        /// @brief 이 컨텍스트가 기록할 버퍼입니다. nullptr 이면 디바이스가 지금 연 버퍼를 따라갑니다.
        VkCommandBuffer _targetBuffer;
        /// @brief 이 컨텍스트가 갱신하는 기록 상태입니다. 리스트가 자기 것을 넘기면 서로 간섭하지 않습니다.
        VulkanRecordingState* _pState;
        /// @brief 슬롯 세트를 할당받는 풀 그룹입니다. nullptr 이면 디바이스 프레임 스트림의 이번 링 슬롯 그룹을 씁니다.
        VulkanDescriptorPoolSet* _pDescriptorPoolSet;
        /// @brief 마지막으로 연 이벤트 마커(패스 이름)입니다. 디바이스를 잃었을 때 "마지막으로 제출한 패스" 를 로그에 적는 데 씁니다.
        fixed_string<constant::buffer::kMax64> _lastEventMarker;
        /// @brief AMD 표지 버퍼에서 이 컨텍스트의 칸 시작입니다 — 그래픽스 0, 비동기 컴퓨트 2(큐마다 제 칸이라 두 큐의 쓰기가 겹치지 않는다).
        uint32 _breadcrumbSlotBase;
    };
} // namespace sw
