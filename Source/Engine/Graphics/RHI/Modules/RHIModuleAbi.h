/**
 * @file RHIModuleAbi.h
 * @brief RHI MODULE 의 C ABI 버전과 스탬프입니다(Engine 과 RHI_* 가 일치해야 합니다).
 *
 * IRHIDevice / IRHIResource / IRHICommandList / IRHICommandContext 의 public 기록 표면이
 * 바이너리 비호환으로 바뀌면 kRHIModuleAbiVersion 이나 kRHIModuleAbiStamp 를
 * 올립니다. Engine 과 함께 모든 RHI_* 모듈을 다시 빌드하십시오.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;

    inline constexpr uint32 kRHIModuleAbiVersion = 6;
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
     *              패스별 GPU 시간을 재는 길. 없을 때는 백프레셔 대리값으로 추측해야 했고 실제로 틀렸다. */
    inline constexpr auto kRHIModuleAbiStamp = "rhi-cl-v16-2026-09";

    using PFN_CreateRHIDevice        = IRHIDevice* (*)();
    using PFN_GetRHIModuleAbiVersion = uint32 ( * )();
    using PFN_GetRHIModuleAbiStamp   = const utf8* (*)();
} // namespace sw
