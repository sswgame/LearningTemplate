/**
 * @file IRHICommandList.h
 * @brief GPU 명령을 기록하는 커맨드 리스트 인터페이스입니다.
 * @details 기록 API(뷰포트, PSO, 렌더 패스, 바인딩, 드로우)를 이 한 곳에 모은다.
 *          `IRHICommandContext` 가 이 인터페이스를 상속해 "기록 범위가 없는" 형태로 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class IRHICommandList
     * @brief GPU 그래픽스 명령과 컴퓨트 명령을 기록하는 커맨드 리스트입니다.
     */
    class SW_API IRHICommandList
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명: 복사 금지
        // ------------------------------------------------------------------------------
        virtual ~IRHICommandList()                           = default;
        IRHICommandList()                                    = default;
        IRHICommandList( const IRHICommandList& )            = delete;
        IRHICommandList& operator=( const IRHICommandList& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) 기록 범위, 뷰포트, PSO, 렌더 패스
        // ------------------------------------------------------------------------------
        virtual void beginCommandList() = 0;
        virtual void endCommandList()   = 0;
        /**
         * @brief 뷰포트를 설정합니다. **가위 사각형도 이 뷰포트 전체로 되돌립니다**(`beginRenderPass` 도 그 패스 크기로 되돌립니다).
         * @details 가위를 설정하고 잊어버려도 다음 뷰포트나 다음 패스로 새지 않는다(언리얼 `RHISetViewport` 가 가위를 끄는 것과 같다).
         */
        virtual void setViewport( const RHIViewport& viewport ) = 0;
        /**
         * @brief 가위 사각형을 설정합니다. 이 밖의 픽셀은 그리지 않으며, 다음 `setViewport` 나 `beginRenderPass` 까지 유지됩니다.
         * @details 단위는 렌더 타깃 픽셀이고 원점은 왼쪽 위다(`RHIViewport` 와 같다). 클리어와 `blitTexture` 는 가위를 따르지 않는다(D3D, Vulkan 규약이며
         *          GL 은 그동안 가위 테스트를 끈다). 축 정렬 사각 클리핑 전용이고, 둥글거나 회전한 클리핑은 셰이더가 처리한다.
         */
        virtual void setScissorRect( const RHIScissorRect& rect ) = 0;
        /**
         * @brief 이 지점의 GPU 시각을 슬롯에 기록합니다. 지원하지 않는 백엔드에서는 아무 일도 하지 않습니다.
         * @param slotIndex `IRHITimestampProvider::getTimestampSlotCount()` 미만이어야 합니다.
         */
        virtual void writeTimestamp( uint32 slotIndex ) { (void)slotIndex; }

        virtual void setPipelineState( RHIPipelineStateHandle pso )             = 0;
        virtual void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) = 0;
        virtual void endRenderPass()                                            = 0;

        // ------------------------------------------------------------------------------
        // 3) 드로우: 메시 버텍스/인덱스, 머티리얼 CB
        // ------------------------------------------------------------------------------
        virtual void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset = 0 ) = 0;
        /**
         * @brief 삼각형 리스트를 그립니다.
         * @details 상수 버퍼(b0/b1)는 여기 인자가 아니라 **bindConstantBuffer( index, shaderslot::k*ConstantBuffer )** 로
         *          미리 바인딩한다. 슬롯은 항상 이름(계약 상수)으로 지정한다. 상수 버퍼를 위치 인자로 받으면 순서를 하나
         *          바꿔 넘긴 호출 한 줄이 네 백엔드에서 동시에 검은 화면을 낸다.
         */
        virtual void draw( uint32 vertexCount, uint32 startVertex = 0 ) = 0;
        /**
         * @brief 삼각형 리스트를 인스턴싱으로 그립니다.
         * @details 지금 씬 드로우는 이 함수가 아니라 `drawIndirect` 를 쓴다(인스턴스 슬롯은 인스턴스 슬롯 스트림이 준다).
         * @param instanceCount 인스턴스 개수입니다.
         * @param startInstance 시작 인스턴스입니다. `SV_InstanceID` 가 이 값을 포함하는지는 API 마다 다르므로, 셰이더가 인스턴스 위치를
         *                      알아야 하면 인스턴스 슬롯 스트림(`constant::kInstanceSlotStreamSlot`)으로 받습니다.
         */
        virtual void drawInstanced( uint32 vertexCount, uint32 instanceCount, uint32 startVertex = 0, uint32 startInstance = 0 ) = 0;
        virtual void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride = 4, uint32 offset = 0 )                         = 0;

        // ------------------------------------------------------------------------------
        // 4) 컴퓨트: PSO, 디스패치, 루트 상수, UAV
        // ------------------------------------------------------------------------------
        /**
         * @brief 컴퓨트 PSO 를 건다. 등록되지 않은 핸들도 기록하므로 앞 컴퓨트 PSO 가 남지 않는다.
         * @note 디스패치(`dispatchCompute` · `dispatchIndirect`)는 **등록된 컴퓨트 PSO 가 걸려 있어야** 한다 — 없으면 네 백엔드 모두 오류로 멈춘다(드로우의 그래픽스 PSO 와 같은 계약).
         *       PSO 가 0 이면 부르는 쪽이 디스패치를 건너뛰는 것이 렌더러의 규칙이다.
         */
        virtual void setComputePipelineState( RHIPipelineStateHandle pso )                                           = 0;
        virtual void dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ ) = 0;
        /**
         * @brief 컴퓨트 루트 상수(푸시 상수)를 씁니다.
         * @details 백엔드마다 실제 용량(dword)이 다르다. DX11 은 64, OpenGL 은 64, DX12 와 Vulkan 은 16이다(루트 상수와 푸시 상수,
         *          bindingslots.hlsli 의 SW_ROOT_DWORD_COUNT). 네 백엔드 모두에서 안전한
         *          상한은 shaderslot::kRootConstantDwords 이고, 그보다 크면 조용히 잘린다.
         */
        virtual void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) = 0;
        /**
         * @brief 그래픽스 스테이지에 루트 상수(푸시 상수)를 씁니다. **드로우마다 바뀌는 소수의 값** 전용입니다.
         * @details 언리얼이 `FMeshDrawCommand` 의 느슨한 파라미터를 드로우별로 싣는 방식에 해당한다. 상수 버퍼로
         *          보내면 드로우마다 버퍼를 새로 잡거나(할당과 디스크립터) 덮어써야 한다. 덮어쓰면 GPU 는 마지막
         *          값만 본다. 루트 상수와 푸시 상수는 커맨드 리스트에 값이 그대로 실려 그 문제가 없다.
         *          용량은 `shaderslot::kRootConstantDwords` 까지가 네 백엔드 공통 안전선이다.
         *          DX12 는 루트 상수, Vulkan 은 푸시 상수를 쓰고, DX11 과 GL 은 계약 슬롯 b2 의 상수 버퍼로 흉내 낸다.
         */
        virtual void setGraphicsRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues = 0 ) = 0;
        virtual void bindComputeUav( RHIDescriptorIndex index, uint32 slot )                                                                             = 0;
        virtual void bindShaderResource( RHIDescriptorIndex index, uint32 slot )                                                                         = 0;

        // ------------------------------------------------------------------------------
        // 4-1) 리플렉션 기반 바인딩: 셰이더가 선언한 레지스터로 CB/SRV 버퍼를 바인딩한다.
        //      (ShaderParameterBinder 가 ShaderBindingLayout 의 _registerIndex 를 slot 으로 전달한다.)
        // ------------------------------------------------------------------------------
        /**
         * @brief 상수 버퍼를 지정한 레지스터 슬롯(bN)에 바인딩합니다.
         * @param constantBufferIndex bindless 디스크립터 인덱스입니다(엔진 CB, 머티리얼 CB).
         * @param slot                HLSL `register(bN)` 의 N 입니다.
         */
        virtual void bindConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) = 0;

        /**
         * @brief 기록 중에 상수 버퍼 내용을 갱신합니다. 이 리스트의 드로우와 디스패치가 이 값을 봅니다(언리얼 `RHIUpdateUniformBuffer( RHICmdList, … )`).
         * @details 프레임 밖(에셋 로드, 머티리얼 파라미터)에서 쓰는 것은 `IRHIResourceFactory::updateConstantBuffer` 이고, 기록 경로(패스 CB,
         *          컴퓨트 CB)는 이 함수를 쓴다. DX11 은 이 리스트의 Deferred Context 에 `Map(WRITE_DISCARD)` 해서 런타임이 리스트 단위로 버퍼를
         *          버저닝하고, DX12, Vulkan, GL 은 버퍼의 이번 프레임 슬롯에 쓴다. 그래서 **한 버퍼는 프레임에 한 번만** 쓴다
         *          (드로우마다 다른 값이면 슬롯을 나눈다. `PassConstantRing` 참고). 크기 상한은 `IRHIResourceFactory::updateConstantBuffer` 와 같다.
         */
        virtual void updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size ) = 0;

        /**
         * @brief 구조화 버퍼 또는 바이트 주소 SRV 버퍼를 지정한 레지스터 슬롯(tN)에 바인딩합니다.
         * @param index bindless 디스크립터 인덱스입니다.
         * @param slot  HLSL `register(tN)` 의 N 입니다.
         */
        virtual void bindStructuredBuffer( RHIDescriptorIndex index, uint32 slot ) = 0;

        /**
         * @brief 컴퓨트 스테이지에 상수 버퍼를 지정한 레지스터 슬롯(bN)에 바인딩합니다.
         * @details 컴퓨트 패스(gpucull 등) 전용이다. bindConstantBuffer 는 그래픽스 스테이지만 대상으로 한다.
         * @param constantBufferIndex bindless 디스크립터 인덱스입니다.
         * @param slot                HLSL `register(bN)` 의 N 입니다.
         */
        virtual void bindComputeConstantBuffer( RHIDescriptorIndex constantBufferIndex, uint32 slot ) = 0;

        /**
         * @brief 컴퓨트 스테이지에 읽기 전용 구조화 SRV 버퍼를 지정한 레지스터 슬롯(tN)에 바인딩합니다.
         * @param index bindless 디스크립터 인덱스입니다.
         * @param slot  HLSL `register(tN)` 의 N 입니다.
         */
        virtual void bindComputeShaderResource( RHIDescriptorIndex index, uint32 slot ) = 0;

        // ------------------------------------------------------------------------------
        // 5) 배리어와 blit: 샘플링 가능 상태로 전환, 컬러 복사 (dst==0은 스왑체인)
        // ------------------------------------------------------------------------------
        virtual void prepareTextureForShaderRead( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처를 렌더 타깃(또는 뎁스)으로 쓸 수 있는 상태로 만듭니다.
         * @details `prepareTextureForShaderRead` 와 짝을 이룬다. 그래프가 레벨을 **병렬로 기록하기 전에**
         *          단일 스레드에서 미리 불러 두는 용도다. 그러면 패스 콜백이 이미 맞는 상태를 보므로
         *          기록 중에 리소스 상태 같은 공유 데이터를 바꾸지 않는다. 배리어를 기록
         *          스레드에서 결정하던 구조는 이 프로젝트에서 실제로 여러 번 깨졌다.
         * @note 컬러인지 뎁스인지는 **백엔드가 자기 텍스처 레코드를 보고 판단한다.** 호출하는 쪽이
         *       그것을 알아내려면 첨부 포맷을 다시 뒤져야 하고, 그러다 틀리면 배리어가 어긋난다.
         * @note GL 은 상태가 없어 아무 일도 하지 않는다. DX11 은 전이가 없지만 이 텍스처가 바인딩된 PS SRV 슬롯을 해제한다. 그대로 두면
         *       첨부로 바인딩할 때 런타임이 SRV 를 NULL 로 강제하고 해저드 경고를 낸다.
         */
        virtual void prepareTextureForRenderTarget( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처를 컴퓨트가 쓰는 RW 텍스처(UAV) 상태로 만듭니다. 디스패치 전에 부릅니다.
         * @details DX12 는 UNORDERED_ACCESS 전이이고, Vulkan 은 GENERAL 레이아웃 전이다. DX11 은 할 일이 없고, GL 은 이미지 접근 배리어다.
         *          다시 샘플링하려면 prepareTextureForShaderRead 를 부른다.
         */
        virtual void prepareTextureForUnorderedAccess( RHITextureHandle texture ) = 0;

        /**
         * @brief 텍스처 전체를 다른 텍스처로 복사합니다. **0 은 백버퍼다.** dst 가 0 이면 화면으로 내보내고, src 가 0 이면 화면에 나갈 이미지를 읽는다(테스트와 진단용).
         * @details 둘 다 0 이면 아무것도 하지 않는다. 백버퍼에서 텍스처로 복사할 때는 크기가 백버퍼와 같아야 하고, D3D 는 포맷도 같아야 한다(`getBackBufferFormat` 으로 만든다.
         *          Vulkan 은 블릿이라 변환한다). GL 은 창의 0행이 아래쪽이라 읽을 때 뒤집으므로, 결과 텍스처의 0행은 항상 화면 위쪽이다.
         *          주의: 백버퍼는 Present 뒤에 내용이 버려지므로(플립 모델) Present 전에 프레임 스트림에 기록한다.
         */
        virtual void blitTexture( RHITextureHandle src, RHITextureHandle dst ) = 0;

        // ------------------------------------------------------------------------------
        // 6) 인디렉트 — 드로우/디스패치, 버퍼 상태 전이
        // 6) 인디렉트: 드로우, 디스패치, 버퍼 상태 전이
        /**
         * @brief 간접 인자로 그립니다. **한 번 그리는 것은 drawCount == 1 인 멀티 드로우입니다.**
         * @details 단일 드로우와 멀티 드로우를 한 함수로 둔다. 개수만 다른 두 경로로 나누면 안 쓰이는 쪽이 조용히 틀린다
         *          (PSO 프로그램, 토폴로지, 정점 버퍼 바인딩 누락).
         * @param argumentBuffer       `RHIDrawIndirectCommand` 배열입니다.
         * @param argumentBufferOffset 첫 커맨드의 바이트 오프셋입니다.
         * @param drawCount            그릴 커맨드 수입니다. 커맨드는 연속으로 놓여 있다고 봅니다.
         * @param countBuffer          실제 개수를 GPU 가 기록해 두는 버퍼입니다(0 이면 drawCount 를 그대로 씁니다).
         *                             지원하지 않는 백엔드는 drawCount 로 폴백합니다.
         * @param countBufferOffset    그 버퍼 안의 바이트 오프셋입니다.
         */
        virtual void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0, uint32 drawCount = 1,
                                   RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 ) = 0;
        /**
         * @brief 스레드 그룹 수를 GPU 가 기록해 둔 버퍼로 컴퓨트를 디스패치합니다.
         * @param argumentBuffer `RHIDispatchIndirectCommand` 하나입니다. **그 구조체가 이 버퍼의 레이아웃 기준입니다.**
         *        값을 채우는 것은 C++ 이 아니라 컴퓨트 셰이더라서, 여기 말고는 그 이름이 불릴 곳이 없습니다.
         * @param argumentBufferOffset 그 버퍼 안의 바이트 오프셋입니다.
         */
        virtual void dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        /**
         * @brief 버퍼를 @p newState 로 쓰기 전에 앞선 접근이 끝나고 보이게 합니다.
         * @details 백엔드마다 상태를 이렇게 옮긴다. 네 백엔드의 `switch` 는 `default:` 없이 모든 `RHIBufferState` 를 다뤄(DX11 은 UAV 를 뗄지, 나머지는 아래 값) 새 상태를 더하면
         *          컴파일 경고로 모든 백엔드가 결정을 요구한다. 텍스처 상태는 RHI 계약에 없다(백엔드가 렌더 패스 · 복사 안에서 스스로 옮긴다).
         *
         *          | 상태 | DX11 | DX12 (리소스 상태) | Vulkan (접근 / 스테이지) | GL (`glMemoryBarrier`) |
         *          |---|---|---|---|---|
         *          | Common | 컴퓨트 UAV 를 뗀다 | COMMON | 0 / TOP_OF_PIPE | 없음 |
         *          | ShaderResource | UAV 를 뗀다 | NON_PIXEL · PIXEL_SHADER_RESOURCE | SHADER_READ / 정점 · 프래그먼트 · 컴퓨트 | SHADER_STORAGE · SHADER_IMAGE_ACCESS |
         *          | UnorderedAccess | 없음(`bindComputeUav` 가 건다) | UNORDERED_ACCESS | SHADER_WRITE · READ / 컴퓨트 · 프래그먼트 | SHADER_STORAGE |
         *          | IndirectArgument | UAV 를 뗀다 | INDIRECT_ARGUMENT | INDIRECT_COMMAND_READ / DRAW_INDIRECT | SHADER_STORAGE · COMMAND |
         *          | CopyDest | UAV 를 뗀다 | COPY_DEST | TRANSFER_WRITE / TRANSFER | BUFFER_UPDATE |
         *          | CopySource | UAV 를 뗀다 | COPY_SOURCE | TRANSFER_READ / TRANSFER | BUFFER_UPDATE |
         *          | VertexOrConstant | UAV 를 뗀다 | VERTEX_AND_CONSTANT_BUFFER | VERTEX_ATTRIBUTE_READ · UNIFORM_READ / 정점 입력 · 정점 · 프래그먼트 · 컴퓨트 | VERTEX_ATTRIB_ARRAY · UNIFORM |
         *          | Index | UAV 를 뗀다 | INDEX_BUFFER | INDEX_READ / 정점 입력 | ELEMENT_ARRAY |
         *
         *          GL 은 버퍼를 가리지 않고 전역 배리어를 낸다. 컴퓨트가 쓴 버퍼를 SSBO 가 아니라 UBO 로 읽으려면 `VertexOrConstant` 로 넘겨야 `UNIFORM` 비트가 걸린다.
         */
        virtual void transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState ) = 0;
        /**
         * @brief 버퍼 앞쪽 @p sizeBytes 를 읽기 버퍼(`IRHIResourceFactory::createReadbackBuffer`)로 복사합니다. GPU 가 끝낸 뒤 CPU 가 읽는 비동기 되읽기의 기록 쪽입니다.
         * @param source    복사할 버퍼. 부르기 전에 `transitionBuffer( source, RHIBufferState::CopySource )` 로 옮겨 둔다(DX12 · Vulkan 은 상태가 맞아야 한다).
         * @param readback  `createReadbackBuffer` 가 만든 버퍼. 원본보다 작지 않아야 한다.
         * @details 복사는 이 리스트의 GPU 순서에 들어간다. 끝났는지는 `IRHIDevice::enqueueGPURelease` 콜백이 알려 주고, 그 콜백 안에서 `readReadbackBuffer` 로 읽는다.
         *          렌더 패스 밖에서 불러야 한다(Vulkan 은 열린 패스를 닫는다).
         */
        virtual void copyBufferToReadback( RHIBufferHandle source, RHIBufferHandle readback, uint32 sizeBytes ) = 0;
        /**
         * @brief 앞선 디스패치의 UAV 쓰기가 끝난 뒤에 다음 디스패치가 읽도록 막습니다.
         * @details 상태가 바뀌지 않는 전이(UAV → UAV)는 `transitionBuffer` 가 아무것도 하지 않는다.
         *          그런데 컴퓨트 두 개가 **같은 버퍼를 이어서** 쓰고 읽으면 그 사이에 장벽이 필요하다.
         *          컬링이 채운 가시 목록을 정렬이 바로 읽는 경우가 그렇다. 없으면 정렬이 아직 채워지지 않은
         *          목록을 읽는다(드라이버와 백엔드마다 결과가 달라져 재현이 어렵다).
         *          DX12 는 UAV 배리어, Vulkan 은 버퍼 메모리 배리어, GL 은 glMemoryBarrier 를 쓰고,
         *          DX11 은 디스패치가 컨텍스트에서 직렬화되므로 할 일이 없다.
         */
        virtual void uavBarrier( RHIBufferHandle buffer ) = 0;
        /**
         * @brief 인덱스 버퍼를 쓰는 드로우를 GPU 가 기록해 둔 인자로 한 번 발행합니다.
         * @param argumentBuffer `RHIDrawIndexedIndirectCommand` 하나입니다. 위와 같은 이유로 그 구조체가 레이아웃 기준입니다.
         * @param argumentBufferOffset 그 버퍼 안의 바이트 오프셋입니다.
         */
        virtual void drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0 ) = 0;
        // ------------------------------------------------------------------------------
        // 7) GPU 디버그 마커
        // ------------------------------------------------------------------------------
        virtual void beginEventMarker( const utf8* pName ) = 0;
        virtual void endEventMarker()                      = 0;
    };
} // namespace sw
