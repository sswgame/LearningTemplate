/**
 * @file IRHICommandList.h
 * @brief GPU 명령을 기록하는 커맨드 리스트 인터페이스
 * @details 기록 API 표면(뷰포트·PSO·렌더패스·바인딩·드로우)을 여기 한곳에 모은다.
 *          `IRHICommandContext` 가 이걸 상속해 "기록 범위가 없는" 형태로 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHICommandList
     * @brief GPU 그래픽스/컴퓨트 명령을 기록하는 커맨드 리스트
     */
    class SW_API IRHICommandList
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — 복사 금지
        // ------------------------------------------------------------------------------
        virtual ~IRHICommandList()                           = default;
        IRHICommandList()                                    = default;
        IRHICommandList( const IRHICommandList& )            = delete;
        IRHICommandList& operator=( const IRHICommandList& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) 기록 범위 · 뷰포트 · PSO · 렌더 패스
        // ------------------------------------------------------------------------------
        virtual void beginCommandList()                                         = 0;
        virtual void endCommandList()                                           = 0;
        virtual void setViewport( const RHIViewport& viewport )                 = 0;
        virtual void setPipelineState( RHIPipelineStateHandle pso )             = 0;
        virtual void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;
        virtual void endRenderPass()                                            = 0;

        // ------------------------------------------------------------------------------
        // 3) 드로우 — 메시 버텍스/인덱스, 머티리얼 CB
        // ------------------------------------------------------------------------------
        virtual void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) = 0;
        /**
         * @brief 삼각형 리스트를 그립니다.
         * @details 상수버퍼(b0/b1)는 여기 인자가 아니라 **bindConstantBuffer( index, shaderslot::k*ConstantBuffer )** 로
         *          미리 건다. 예전엔 draw( …, passCb, materialCb ) 위치 인자였는데, 자리를 하나 바꿔 넘긴 호출 한 줄이
         *          네 백엔드에서 동시에 검은 화면을 냈다 — 슬롯은 항상 이름(계약 상수)으로 지정한다.
         */
        virtual void draw( uint32 vertexCount, uint32 startVertex = 0 ) = 0;
        /**
         * @brief 삼각형 리스트를 인스턴스드로 그립니다 (GPUScene 인스턴스 버퍼 경로).
         * @param instanceCount 인스턴스 개수. VS 는 `SV_InstanceID` 로 `g_SwInstances[g_InstanceBase + id]` 를 읽습니다.
         * @param startInstance 시작 인스턴스 위치. 크로스 백엔드 일관성을 위해 셰이더 오프셋은 `g_InstanceBase` 로 넘기고
         *                      이 값은 0 을 권장합니다 (SV_InstanceID 는 백엔드마다 base 포함 여부가 다름).
         */
        virtual void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) = 0;
        virtual void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 )                         = 0;

        // ------------------------------------------------------------------------------
        // 4) 컴퓨트 — PSO, 디스패치, 루트 상수, UAV
        // ------------------------------------------------------------------------------
        virtual void setComputePipelineState( RHIPipelineStateHandle pso )                                           = 0;
        virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) = 0;
        /**
         * @brief 컴퓨트 루트/푸시 상수를 씁니다.
         * @details 백엔드마다 실제 용량(dword)이 다르다 — DX11=64, OpenGL=64, DX12/Vulkan=16 (루트/푸시 상수,
         *          bindingslots.hlsli 의 SW_ROOT_DWORD_COUNT). 4개 백엔드 모두에서 안전한
         *          상한은 constant::kMinComputeRootConstantDwords — 그 이상은 조용히 잘린다.
         */
        virtual void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) = 0;
        /**
         * @brief 그래픽스 스테이지에 루트/푸시 상수를 씁니다 — **드로우마다 바뀌는 소수의 값** 전용.
         * @details 언리얼이 `FMeshDrawCommand` 의 느슨한 파라미터를 드로우별로 싣는 자리와 같다. 상수버퍼로
         *          나르면 드로우마다 버퍼를 새로 잡거나(할당·디스크립터) 덮어써야 하는데(덮어쓰면 GPU 는 마지막
         *          값만 본다), 루트/푸시 상수는 커맨드 리스트에 값이 그대로 실려 그 문제가 없다.
         *          용량은 `constant::kMinComputeRootConstantDwords` 까지가 4 백엔드 공통 안전선이다.
         *          DX12 는 루트 상수, Vulkan 은 푸시 상수, DX11/GL 은 계약 슬롯 b2 의 상수버퍼로 흉내 낸다.
         */
        virtual void setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) = 0;
        virtual void bindComputeUAV( RHIDescriptorIndex index, uint32 slot )                                                                             = 0;
        virtual void bindShaderResource( RHIDescriptorIndex index, uint32 slot )                                                                         = 0;

        // ------------------------------------------------------------------------------
        // 4-1) 리플렉션 구동 바인딩 — 셰이더가 선언한 레지스터로 CB/SRV 버퍼를 바인딩
        //      (ShaderBindingBinder 가 ShaderBindingLayout 의 _registerIndex 를 slot 으로 전달)
        // ------------------------------------------------------------------------------
        /**
         * @brief 상수 버퍼를 지정한 레지스터 슬롯(bN)에 바인딩합니다.
         * @param constantBufferIndex bindless 디스크립터 인덱스 (엔진/머티리얼 CB).
         * @param slot                HLSL `register(bN)` 의 N. 네이티브 bindless 백엔드는 루트상수/CB에 인덱스만 기록해도 됩니다.
         */
        virtual void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) = 0;

        /**
         * @brief 구조적/바이트주소 SRV 버퍼를 지정한 레지스터 슬롯(tN)에 바인딩합니다.
         * @param index bindless 디스크립터 인덱스.
         * @param slot  HLSL `register(tN)` 의 N.
         */
        virtual void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) = 0;

        /**
         * @brief 컴퓨트 스테이지에 상수 버퍼를 지정한 레지스터 슬롯(bN)에 바인딩합니다.
         * @details gpucull 등 컴퓨트 패스 전용 — bindConstantBuffer 는 그래픽스 스테이지만 대상으로 합니다.
         * @param constantBufferIndex bindless 디스크립터 인덱스.
         * @param slot                HLSL `register(bN)` 의 N.
         */
        virtual void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) = 0;

        /**
         * @brief 컴퓨트 스테이지에 읽기전용 구조적 SRV 버퍼를 지정한 레지스터 슬롯(tN)에 바인딩합니다.
         * @param index bindless 디스크립터 인덱스.
         * @param slot  HLSL `register(tN)` 의 N.
         */
        virtual void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) = 0;

        // ------------------------------------------------------------------------------
        // 5) 배리어 · blit — 샘플링 가능 전환, 컬러 복사 (dst==0은 스왑체인)
        // ------------------------------------------------------------------------------
        virtual void prepareTextureForShaderRead( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처를 렌더타깃(또는 뎁스)으로 쓸 수 있는 상태로 만듭니다.
         * @details `prepareTextureForShaderRead` 의 짝이다. 그래프가 웨이브를 **병렬로 기록하기 전에**
         *          단일 스레드에서 미리 불러 두는 용도다 — 그러면 패스 콜백은 이미 맞는 상태를 보게
         *          되어, 기록 중에 리소스 상태 같은 공유 데이터를 바꾸지 않는다. 배리어를 기록
         *          스레드에서 결정하던 구조는 이 프로젝트에서 실제로 여러 번 깨졌다.
         * @note 컬러인지 뎁스인지는 **백엔드가 자기 텍스처 레코드를 보고 판단한다** — 호출부가
         *       그걸 알아내려면 첨부 포맷을 다시 뒤져야 하고, 그러다 틀리면 배리어가 어긋난다.
         * @note DX11/GL 은 상태리스라 no-op 이다.
         */
        virtual void prepareTextureForRenderTarget( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처를 컴퓨트가 쓰는 RW 텍스처(UAV) 상태로 만듭니다 — 디스패치 전에 부른다.
         * @details DX12 는 UNORDERED_ACCESS 전이, Vulkan 은 GENERAL 레이아웃 전이. DX11 은 no-op, GL 은 이미지 접근 배리어.
         *          다시 샘플링하려면 prepareTextureForShaderRead 를 부른다.
         */
        virtual void prepareTextureForUnorderedAccess( RHITextureHandle texture ) = 0;

        virtual void blitTexture( RHITextureHandle src, RHITextureHandle dst ) = 0;

        // ------------------------------------------------------------------------------
        // 6) 인디렉트 — 드로우/디스패치, 버퍼 상태 전이
        // ------------------------------------------------------------------------------
        /**
         * @brief 간접 인자로 그립니다. **한 번 그리는 것은 drawCount == 1 인 멀티 드로우다.**
         * @details 예전엔 drawIndirect 와 multiDrawIndirect 가 따로 있었다. 둘은 같은 일을 하는데 개수만
         *          다르고, 나뉘어 있는 동안 **멀티 쪽만 조용히 틀려 있었다** — GL 은 PSO 프로그램·토폴로지를
         *          안 걸고 GL_TRIANGLES 로 굳혔고, Vulkan 은 정점버퍼 바인딩을 빠뜨렸다. 아무도 안 부르는
         *          경로라 드러나지 않았다. 하나로 합치면 그럴 자리가 없다.
         * @param argumentBuffer       `RHIDrawIndirectCommand` 배열.
         * @param argumentBufferOffset 첫 커맨드의 바이트 오프셋.
         * @param drawCount            그릴 커맨드 수. 커맨드는 연속으로 놓여 있다고 본다.
         * @param countBuffer          실제 개수를 GPU 가 적어 두는 버퍼(0 이면 drawCount 를 그대로 쓴다).
         *                             지원하지 않는 백엔드는 drawCount 로 폴백한다.
         * @param countBufferOffset    그 버퍼 안의 바이트 오프셋.
         */
        virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                                   RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) = 0;
        /**
         * @brief 스레드 그룹 수를 GPU 가 적어 둔 버퍼로 컴퓨트를 디스패치합니다.
         * @param argumentBuffer `RHIDispatchIndirectCommand` 하나. **그 구조체가 이 버퍼의 레이아웃 정본이다** —
         *        값을 채우는 것은 C++ 이 아니라 컴퓨트 셰이더라, 여기 말고는 그 이름이 불릴 자리가 없다.
         * @param argumentBufferOffset 그 버퍼 안의 바이트 오프셋.
         */
        virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        virtual void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )                 = 0;
        /**
         * @brief 앞선 디스패치의 UAV 쓰기가 끝난 뒤에 다음 디스패치가 읽도록 막습니다.
         * @details 상태가 바뀌지 않는 전이(UAV → UAV)는 `transitionBuffer` 가 아무것도 하지 않는다 —
         *          그런데 컴퓨트 두 개가 **같은 버퍼를 이어서** 쓰고 읽으면 그 사이에 장벽이 필요하다.
         *          컬링이 채운 가시 목록을 정렬이 바로 읽는 자리가 그렇다. 없으면 정렬이 아직 안 채워진
         *          목록을 읽는다(드라이버·백엔드마다 결과가 달라져 재현이 어렵다).
         *          DX12 는 UAV 배리어, Vulkan 은 버퍼 메모리 배리어, GL 은 glMemoryBarrier,
         *          DX11 은 디스패치가 컨텍스트에서 직렬화되므로 할 일이 없다.
         */
        virtual void uavBarrier( RHIBufferHandle buffer ) = 0;
        /**
         * @brief 인덱스 버퍼를 쓰는 드로우를 GPU 가 적어 둔 인자로 한 번 발행합니다.
         * @param argumentBuffer `RHIDrawIndexedIndirectCommand` 하나. 위와 같은 이유로 그 구조체가 레이아웃 정본이다.
         * @param argumentBufferOffset 그 버퍼 안의 바이트 오프셋.
         */
        virtual void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        // ------------------------------------------------------------------------------
        // 7) GPU 디버그 마커
        // ------------------------------------------------------------------------------
        virtual void beginEventMarker( const utf8* pName ) = 0;
        virtual void endEventMarker()                      = 0;
    };
} // namespace sw
