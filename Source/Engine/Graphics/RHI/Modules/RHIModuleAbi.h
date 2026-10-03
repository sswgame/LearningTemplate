/**
 * @file RHIModuleAbi.h
 * @brief RHI MODULE 의 C ABI 버전과 스탬프입니다(Engine 과 RHI_* 가 일치해야 합니다).
 *
 * IRHIDevice / IRHIResource / IRHICommandList / IRHICommandContext 의 public 기록 표면이나
 * 레이아웃(멤버 · 가상 함수)이 바이너리 비호환으로 바뀌면 kRHIModuleAbiVersion 과 kRHIModuleAbiStamp 를
 * **함께** 올립니다(둘이 어긋나면 컴파일되지 않습니다). Engine 과 함께 모든 RHI_* 모듈을 다시 빌드하십시오.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;

    /** @brief 숫자 버전입니다. 도장의 `v<N>` 과 같아야 합니다. 예전에는 도장만 v16 까지 올리고 이 값은 6 에 머물러 둘이 다른 것을 셌습니다. */
    inline constexpr uint32 kRHIModuleAbiVersion = 22;
    /** @brief 불투명 표면 지문입니다. 커맨드 리스트 · 디바이스 ABI 가 바뀌면 문자열을 바꿉니다.
     *         v3: IRHICommandList/ICommandReplayTarget 에 bindConstantBuffer/bindStructuredBuffer 추가.
     *         v4: drawInstanced (인스턴스드 드로우, GPUScene 인스턴스 버퍼) 추가.
     *         v5: bindComputeConstantBuffer/bindComputeShaderResource 추가 (gpucull 컴퓨트 바인딩 수정).
     *         v6: DX12 가 소프트웨어 Cmd-vector-replay(RHIDeferredCommandList) 대신 진짜 네이티브
     *             ID3D12GraphicsCommandList(D3D12RHICommandList)를 IRHICommandList 로 반환 · 제출한다
     *             (Deferred Context 가 진짜 독립 리스트가 됨). DX11/Vulkan/OpenGL 은 다음 커밋에서 이어 감.
     *         v7: DX11/Vulkan/OpenGL 도 소프트웨어 Cmd-vector 없이 즉시 호출하는 네이티브
     *             IRHICommandList 로 전환 완료. 이제 어떤 백엔드도 RHIDeferredCommandList 를 쓰지
     *             않아 그 클래스와 ICommandReplayTarget 을 완전히 삭제. IRHICommandContext 가
     *             ICommandReplayTarget 대신 IRHICommandList 를 직접 상속(같은 기록 API 표면을
     *             공유)하도록 표면 자체가 바뀌었으므로 버전을 올림.
     *         v8: IRHIResource 에 unregisterBindlessTexture 추가. 텍스처 · 버퍼 인덱스 공간이 다른
     *             백엔드(DX11/GL/Vulkan)에서 텍스처 SRV 를 버퍼 해제로 넘기던 오염을 끊는다.
     *         v9: IRHIResource::uploadTexture2D + RHITextureUploadDesc. 처음으로 텍스처에 픽셀을 올리는 길.
     *         v10: readbackTexture2D(동기 읽기) + RHIFormat BC1~BC7. 업로드 내용을 바이트로 검증할 수 있게.
     *         v11: IRHIResource::registerBindlessTextureUav + IRHICommandList::prepareTextureForUnorderedAccess. 컴퓨트 RW 텍스처.
     *         v12: IRHICommandList::setGraphicsRootConstants. 드로우별 상수를 루트 · 푸시 상수로 옮겼다(`724f3ddb`).
     *         v13: IRHICommandList::uavBarrier. 컬링이 채운 가시 목록을 정렬이 바로 읽는 자리(`d9a5ffd7`).
     *         v14: drawIndirect 와 multiDrawIndirect 를 drawIndirect( …, drawCount, countBuffer ) 하나로 합쳤다(`4c7b8d65`).
     *         v15: IRHIResource::updateStructuredBuffer -> updateStructuredBufferRegions (구간 배열 · 목적 버퍼 오프셋).
     *              바뀐 인스턴스만 올리기 위해서다. 8000 개 중 10 개만 움직여도 전체를 올리고 있었다.
     *              updateStructuredBufferRange · updateStructuredBuffer 는 그 위의 비가상 도우미다.
     *         v16: IRHICommandList::writeTimestamp + IRHIDevice::setTimestampEnabled/getTimestampSlotCount/readTimestampsMicros.
     *              패스별 GPU 시간을 재는 길. 없을 때는 백프레셔 대리값으로 추측해야 했고 실제로 틀렸다.
     *         v17: IRHIDevice::waitIdle 이 비가상이 됐다(렌더 스레드를 먼저 비운 뒤 백엔드의 waitIdleInternal 을 부른다) +
     *              setRenderThreadDrain 과 그 멤버. 비동기 씬 로드 · 핫 리로드가 렌더 스레드 기록 도중에 장치 대기를 끼워 넣던 것.
     *         v18: IRHIDevice::supportsMultiRenderTarget 삭제(가상 함수 하나가 빠져 vtable 이 바뀐다). 네 백엔드가 모두 MRT 를 보장한다.
     *         v19: IRHICommandList::updateConstantBuffer 추가. 기록 중의 상수버퍼 갱신이 리스트로 간다(DX11 의 기록 슬롯 토큰 삭제).
     *         v20: 텍스처 모양(RHITextureDimension · RHITextureDesc::_arraySize) · 렌더 패스의 면 고르기(_arrColorTargetSlice · _depthTargetSlice) ·
     *              RHITextureUploadDesc::_arraySlice · readbackTexture2D 의 arraySlice 인자. 배열 · 큐브 텍스처.
     *         v21: IRHIDevice::shutdown 이 비가상 템플릿 메서드가 됐고 종료 3 단계 훅 detachCommandRecordingInternal 이 생겼다(vtable 이 바뀐다).
     *         v22: IRHIDevice::queryNativeHandlesInternal 가상 훅 + RHINativeHandles(판 번호 든 POD). 에디터가 VulkanRHIDevice 로 캐스팅해
     *              부르던 VulkanRHIDevice::queryNativeHandles 를 지웠다. RHINativeHandles 의 레이아웃도 이 도장이 덮는다(Engine ↔ RHI_*),
     *              Engine ↔ 부르는 모듈은 구조체의 _version · _byteSize 로 대조한다. */
    inline constexpr auto kRHIModuleAbiStamp = "rhi-cl-v22-2026-10";

    namespace RHIModuleAbiInternal
    {
        /** @brief 도장 `rhi-cl-v<N>-…` 의 N 을 읽습니다. 숫자 버전과 도장이 어긋나지 않게 컴파일 때 견줍니다. */
        constexpr uint32 readStampVersion( const utf8* pStamp )
        {
            uint32 index{ 0 };
            while ( pStamp[index] != '\0' && pStamp[index] != 'v' )
                ++index;
            if ( pStamp[index] == 'v' )
                ++index;
            uint32 version{ 0 };
            while ( pStamp[index] >= '0' && pStamp[index] <= '9' )
            {
                version = version * 10u + static_cast<uint32>( pStamp[index] - '0' );
                ++index;
            }
            return version;
        }
    } // namespace RHIModuleAbiInternal
    static_assert( RHIModuleAbiInternal::readStampVersion( kRHIModuleAbiStamp ) == kRHIModuleAbiVersion,
                   "kRHIModuleAbiVersion and the v<N> in kRHIModuleAbiStamp must be bumped together" );

    using PFN_CreateRHIDevice        = IRHIDevice* (*)();
    using PFN_GetRHIModuleAbiVersion = uint32 ( * )();
    using PFN_GetRHIModuleAbiStamp   = const utf8* (*)();
} // namespace sw
