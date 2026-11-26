/**
 * @file RHIConstants.h
 * @brief 백엔드 간에 값이 같아야 하는 RHI 계약 상수입니다(`constant::render`).
 */
#pragma once
#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    namespace constant::render
    {
        // ------------------------------------------------------------------------------
        // 백엔드 간에 값이 같아야 하는 계약 상수.
        // 지금 값이 우연히 같더라도 바뀔 수 있는 값이면 반드시 여기로 뺀다. 한쪽 백엔드만
        // 바뀌면 컴파일은 통과하고 런타임에 조용히 깨진다(아래 kBackBufferFormat 사례).
        // ------------------------------------------------------------------------------

        /**
         * @brief 백버퍼 컬러 포맷입니다. 네 백엔드의 스왑체인과 파이프라인 RTV 기본값이 같아야 합니다.
         * @details 파이프라인은 `RHIPipelineStateDesc::_arrRTVFormat` 으로 렌더 패스와 PSO 를 만든다.
         *          스왑체인이 다른 포맷을 고르면 백버퍼에 직접 그리는 패스가 모두 비호환이 된다.
         *          예를 들어 Vulkan 이 `B8G8R8A8_UNORM` 을 고르면 에디터 없이 실행하는 경로가 렌더 패스 비호환으로 깨진다.
         *          백엔드가 이 포맷을 낼 수 없으면 조용히 다른 포맷을 고르지 말고
         *          `IRHIDevice::getBackBufferFormat()` 으로 실제 채택한 값을 보고해야 한다.
         */
        inline constexpr RHIFormat kBackBufferFormat = RHIFormat::R8G8B8A8_UNORM;

        /**
         * @brief 오프스크린 컬러 타깃(에디터 게임 뷰 등)의 기본 포맷입니다.
         * @details Vulkan 은 이 포맷일 때만 공용 오프스크린 렌더 패스를 재사용하고, 나머지는 전용
         *          렌더 패스를 만든다. 값이 갈라지면 렌더 패스가 조용히 늘어나거나 비호환이 된다.
         */
        inline constexpr RHIFormat kOffscreenColorFormat = RHIFormat::R8G8B8A8_UNORM;

        /**
         * @brief 깊이와 스텐실의 기본 포맷입니다.
         * @details DX11, DX12, GL 은 이 값을 그대로 쓴다. Vulkan 은 물리 디바이스가 지원하지 않으면
         *          대체 포맷을 고르고(`selectDepthFormat()`), 파이프라인 렌더 패스도 그 값에 맞춘다.
         */
        inline constexpr RHIFormat kDepthStencilFormat = RHIFormat::D24_UNORM_S8_UINT;

        /**
         * @brief 상수 버퍼 슬롯 정렬(바이트)입니다.
         * @details 네 백엔드가 같은 값으로 슬롯 크기를 계산해야 프레임 링 오프셋이 어긋나지 않는다.
         *          D3D12 의 요구치(256)가 가장 크므로 그것을 공통값으로 쓴다. 텍스처 행 정렬
         *          (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`)은 이름만 같은 별개 개념이므로 섞지 않는다.
         */
        inline constexpr uint32 kConstantBufferAlignment = 256;

        /** @brief submitTextureUpload 가 한 번에 받는 밉 수의 상한입니다. 2^16 텍스처의 전체 체인(17단계)을 덮습니다. */
        inline constexpr uint32 kMaxTextureMipCount = 17;

        /**
         * @brief CPU 가 GPU 를 앞설 수 있는 최대 프레임 수입니다. 프레임별 리소스 링의 슬롯 수와 같습니다.
         * @details 이 개념의 유일한 원본이다. 프레임마다 따로 있어야 하는 자원은 모두 이 값으로 크기를 잡는다.
         *          DX12 의 커맨드 얼로케이터와 업로드 슬롯, Vulkan 의 커맨드 버퍼, 펜스, 상수 버퍼 슬롯, 디스크립터 셋,
         *          에디터 draw 스냅샷이 여기에 해당한다.
         *          별칭은 두지 않는다. 같은 개념에 상수가 둘이면 섞어 쓰다가 한쪽만 올렸을 때 디스크립터가
         *          버퍼 밖을 가리킨다.
         */
        inline constexpr uint32 kMaxFrameCountInFlight = 3;
        /** @brief 놓은 핸들 칸을 다시 쓰기까지 지나야 하는 프레임 경계 수입니다(`RHIHandleTable`) — 놓은 프레임의 기록이 끝나고 그 다음 프레임이 시작한 뒤다. */
        inline constexpr uint64 kHandleReuseDelayCount = 2;

        /**
         * @brief 프레임 하나에 찍을 수 있는 GPU 타임스탬프 슬롯 수입니다.
         * @details 패스 인덱스로 슬롯을 고정해 쓴다(패스당 begin/end 두 슬롯). 네 백엔드가 같은 값으로
         *          쿼리 힙, 풀, 쿼리 배열 크기를 잡으므로 한쪽만 바꾸면 다른 쪽이 구간 밖을 읽는다.
         *          128 슬롯을 렌더러가 뷰 패스 쌍 · 프레임 패스 쌍 · 뒤쪽 예약 세 칸(프레임 전체 · 프레임 그래프)으로 나눈다(`GPUTimestampSlotLayout`).
         *          그래프가 그 몫보다 길어지면 뒤쪽 패스는 시간이 빠지고 로드 때 경고한다. 64 에서 늘린 까닭: 프레임 패스(컬링 · 그리드 · 스킨 …)를 하나씩 재려면
         *          쌍이 열 몇 개 더 필요하다. 쿼리 힙 크기는 칸 × 프레임 수(128 × 3)라 비용이 없다.
         *          슬롯 비트 집합은 `GPUTimestampSlotMask`(64 비트 단어 배열) 하나로 다룬다 — 슬롯 수를 늘려도 단어 수가 따라간다.
         */
        inline constexpr uint32 kMaxGPUTimestampSlot = 128;

        /**
         * @brief 게임 스레드가 만든 프레임 패킷이 렌더 스레드에 소비되기까지 큐에 쌓일 수 있는 최대
         *        프레임 수입니다(RenderThread 패킷 링 깊이).
         * @details 아직 큐에 있는(소비되지 않은) 패킷이 참조할 수 있는 자원은 최소 이 프레임 수만큼
         *          해제를 미뤄야 한다(예: GPUScene 의 머티리얼 원소 회수 지연). GPU 인플라이트
         *          값인 kMaxFrameCountInFlight 와는 별개 개념이므로 혼동하지 않는다.
         */
        inline constexpr uint32 kRenderFrameQueueDepth = 3;

        /**
         * @brief Vulkan 백엔드가 요구하는 최소 API 버전(major.minor)입니다. Vulkan 셰이더 쿠킹 타깃(`-fspv-target-env=vulkan<major>.<minor>`)도 이 값입니다.
         * @details 셰이더 컴파일러(Graphics/Shader)와 Vulkan 물리 디바이스 선택(Graphics/RHI/Vulkan)이 함께 읽는 계약입니다. 쿠킹된 SPIR-V 버전은
         *          이 타깃이 정하므로(vulkan1.2 → SPIR-V 1.5) 한쪽만 바꾸면 디바이스가 받지 못하는 모듈이 남습니다. API 버전 인코딩과 비교는
         *          `VulkanRHIRequiredVersion` 에 있습니다.
         *          1.2 인 까닭: Android 기기 다수와 dzn(D3D12 위의 Vulkan)이 1.2 다. 1.2 타깃의 HLSL `clip` · `discard` 는 OpKill 이라
         *          셰이더가 미분 읽기를 다 마친 뒤 버린다(binding.hlsli 8-1 절) — 1.3 의 shaderDemoteToHelperInvocation 을 요구하지 않는다.
         */
        inline constexpr uint32 kVulkanRequiredMajor = 1;
        inline constexpr uint32 kVulkanRequiredMinor = 2;
    } // namespace constant::render
} // namespace sw
