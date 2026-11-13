/**
 * @file RHICapabilities.h
 * @brief RHI 백엔드 능력 플래그입니다. 백엔드마다의 값과 가용성 조회는 `RHIBackendInfo.h` 의 기술자 테이블에 있습니다.
 */
#pragma once
#include "Engine/Config/RHIBackendType.h"

namespace sw
{
    /**
     * @note 비트필드 대신 uint8 입니다. DLL 과 컴파일러 사이의 패킹, true 대입 문제를 피합니다. 0/1 만 씁니다.
     * @note **여기에 에디터 능력을 넣지 말 것.** "이 백엔드로 에디터가 도는가" 는
     *       디바이스가 아니라 **에디터가 아는 사실**입니다. 에디터 UI 는 RHI 로 그리므로(`ImGuiRenderer`) 그 답은 에디터 렌더러의 초기화(PSO 생성)가 정합니다.
     */
    struct SW_API RHICapabilities
    {
        uint8 _bBindless{ SW_FALSE };       ///< 디스크립터 인덱스 테이블 (드로우 시 바인드로 에뮬 가능)
        uint8 _bNativeBindless{ SW_FALSE }; ///< 하드웨어 디스크립터 인덱싱 / bindless 샘플링
        uint8 _bCompute{ SW_TRUE };         ///< 컴퓨트 셰이더
        uint8 _bGPUCulling{ SW_FALSE };     ///< 컴퓨트 컬 + 인디렉트 인자 경로
        /**
         * @brief GPU 메시 모프입니다. 컴퓨트가 정점을 변형하고 정점 셰이더가 그 결과를 풀링합니다.
         * @details 끄면 모프를 요청한 메시도 **레스트 포즈로 그려집니다**(셰이더의 폴백 경로 그대로).
         *          언리얼도 스킨 캐시를 못 쓰면 일반 정점 팩토리로 되돌립니다. 그리기가 멈추지는 않습니다.
         */
        uint8 _bGPUMeshMorph{ SW_FALSE };
        uint8 _bMultiDrawIndirect{ SW_FALSE };        ///< 멀티 드로우 / count 버퍼 (DX12/VK/GL; DX11은 루프)
        uint8 _bParallelCommandRecording{ SW_FALSE }; ///< 멀티스레드 커맨드 리스트 병렬 기록 및 제출 지원 (DX12/VK)
        uint8 _bRequiresWindowRecreate{ SW_FALSE };   ///< OS 윈도우 픽셀 포맷 1회 제한(Windows WGL 등)으로 핫스왑 시 윈도우 재생성 필요
        /**
         * @brief 워커 스레드에서 GPU 리소스를 **만들어도** 되는지입니다(그리기가 아니라 생성만).
         * @details DX12 · Vulkan 은 디바이스 레벨 생성이 스펙상 스레드 안전하고, DX11 도 ID3D11Device 는
         *          (컨텍스트와 달리) 안전합니다. OpenGL 은 `glGen*` 이 **현재 컨텍스트**를 필요로 해 안 됩니다.
         *          `GPUUploadQueue` 가 이 값으로 워커 병렬과 "받지 않음(렌더 스레드가 만든다)" 을 가릅니다.
         */
        uint8 _bThreadSafeResourceCreation{ SW_FALSE };
        /**
         * @brief 삼각형을 선으로 채우는 래스터(`FillMode::Wireframe`)를 쓸 수 있는지입니다.
         * @details Vulkan 은 디바이스 기능 `fillModeNonSolid` 가 있어야 한다. 없는 디바이스에서 와이어프레임 PSO 를 만들면 백엔드가 거절하고(솔리드로 바꾸지 않는다),
         *          렌더러는 와이어프레임 보기 모드를 받지 않는다(`FrameRenderer::setViewMode`).
         */
        uint8 _bWireframeFill{ SW_FALSE };
        /**
         * @brief GPU 브레드크럼을 쓸 수 있는지입니다 — 패스마다 GPU 가 닿은 자리를 표지로 남겨 디바이스 소실 뒤에 읽는다(`IRHICommandList::writeBreadcrumb` · `IRHIDevice::readLastBreadcrumbs`).
         * @details DX12 는 기록 가능한 읽기 힙 버퍼가 만들어졌을 때, Vulkan 은 `VK_NV_device_diagnostic_checkpoints` 나 `VK_AMD_buffer_marker` 가 켜졌을 때만 참이다(`-gv_gpuBreadcrumbs` 를 끄면 둘 다 거짓).
         *          DX11 · OpenGL 은 GPU 쪽 표지 수단이 없어 늘 거짓이고, 소실 보고는 CPU 가 마지막으로 기록한 패스만 싣는다(`GPUBreadcrumbs::reportDeviceLost`).
         */
        uint8 _bGPUBreadcrumbs{ SW_FALSE };
        /**
         * @brief 머티리얼 셰이더에 헐 · 도메인 스테이지를 붙인 변형(변위 머티리얼 `MATERIAL_DISPLACEMENT`)을 그릴 수 있는지입니다.
         * @details 패스 XML 의 선택 스테이지(`_hullEntryPoint`)와 달리 헐 · 도메인이 머티리얼 버퍼 · 머티리얼 텍스처 · 패스 상수를 읽는다.
         *          DX11 · DX12 는 참, Vulkan 은 장치 기능 `tessellationShader` 가 있을 때 참이다. OpenGL 은 거짓이다 — 구조 버퍼를 두 스테이지가 함께 읽으면
         *          ARB_gl_spirv 드라이버가 링크를 거절한다(binding.hlsli 9 절). WebGPU 는 테셀레이션이 없다. 거짓이면 변형을 만들지 않고 오류를 남긴다
         *          (`FramePSOManager.DisplacementUnsupported`) — 평면으로 대신 그리지 않는다(언리얼도 테셀레이션 없는 플랫폼에서 변위를 빼지만 여기서는 빠진 그림을 숨기지 않는다).
         */
        uint8 _bMaterialTessellation{ SW_FALSE };
        /**
         * @brief 그래픽스 큐와 나란히 도는 비동기 컴퓨트 큐가 있는지입니다(`IRHIDevice::createAsyncComputeCommandList` · `submitAsyncCompute` · `waitForAsyncCompute`).
         * @details DX12 는 COMPUTE 큐와 펜스 둘, Vulkan 은 그래픽스 큐 패밀리의 두 번째 큐(패밀리가 같아 소유권 이전이 없다)와 이진 세마포어로 잇는다 — 그 패밀리에 큐가 하나뿐인
         *          장치는 거짓이다. DX11 · GL · WebGPU 는 API 에 큐가 하나라 늘 거짓이다. 거짓이면 `_queue="AsyncCompute"` 패스는 그래픽스 큐에서 같은 순서로 돈다 —
         *          능력 표가 정한 길이지 실패를 덮는 대체가 아니다(언리얼 `GSupportsEfficientAsyncCompute` 가 거짓이면 RDG 가 AsyncCompute 패스를 그래픽스로 돌리는 것과 같다).
         */
        uint8 _bAsyncCompute{ SW_FALSE };
        /**
         * @brief 픽셀 셰이더가 쓰기 버퍼(UAV · 스토리지 버퍼)에 쓸 수 있는지입니다(`IRHICommandList::bindGraphicsUAV`) — 셰이더 메시지 채널(`GPUMessageChannel`)이 그래픽스 단계에서 쓴다.
         * @details DX11(기능 수준 11) · DX12 · OpenGL 4.3+ 은 늘 참이고, Vulkan 은 디바이스 기능 `fragmentStoresAndAtomics` 가 있어야 참이다. 거짓이면 메시지 채널을 요구한 실행이 초기화에서 멈춘다
         *          (메시지 변형 PSO 를 버퍼 없이 돌리지 않는다).
         */
        uint8 _bGraphicsShaderWrites{ SW_FALSE };
        /**
         * @brief 패스 XML 의 선택 지오메트리 스테이지(`_geometryEntryPoint`)를 PSO 에 붙일 수 있는지입니다.
         * @details DX11 · DX12 · OpenGL 은 참, Vulkan 은 장치 기능 `geometryShader` 가 있을 때 참이다. WebGPU 는 스테이지가 없다. 거짓이면 그 PSO 를 만들지 않는다(백엔드가 거절한다) —
         *          자동화 시나리오는 감시 값 `RHI.GeometryStage` 로 요구를 선언하고 건너뛴다(언리얼 `RHISupportsGeometryShaders` · 유니티 `SystemInfo.supportsGeometryShaders` 와 같은 자리).
         */
        uint8 _bGeometryStage{ SW_FALSE };
        /**
         * @brief 패스 XML 의 선택 헐 · 도메인 스테이지(`_hullEntryPoint`)를 PSO 에 붙일 수 있는지입니다. 머티리얼 변위(`_bMaterialTessellation`)와 달리 머티리얼 자원을 읽지 않는다.
         * @details DX11 · DX12 · OpenGL 은 참, Vulkan 은 장치 기능 `tessellationShader` 가 있을 때 참이다(dzn 은 없다). WebGPU 는 스테이지가 없다. 감시 값은 `RHI.TessellationStages` 다
         *          (언리얼 `RHISupportsTessellation` · 유니티 `SystemInfo.supportsTessellationShaders` 와 같은 자리).
         */
        uint8 _bTessellationStages{ SW_FALSE };
        /**
         * @brief 서술이 다른 렌더 타깃 텍스처를 힙 하나에 겹쳐 놓을 수 있는지입니다(`IRHIResourceFactory::createTransientHeap` · `createPlacedTexture2D`, 첫 쓰기 앞 `IRHICommandList::beginTextureAlias`).
         * @details DX12 는 placed resource(D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES 힙 — 모든 리소스 힙 등급이 받는다)와 앨리어싱 배리어로 참이다. Vulkan 도 참이다 — 메모리 하나에 이미지를
         *          겹쳐 묶고(`vkBindImageMemory` 오프셋), 해제 큐의 대리자가 힙 메모리 참조를 펜스까지 들어 마지막 이미지가 사라진 뒤에 메모리를 놓는다. DX11 · GL · WebGPU 는 API 에 없어 거짓이다.
         *          거짓이면 트랜지언트 풀은 같은 서술끼리만 텍스처를 나눈다(`RenderGraphAliasPlan`) — 능력 표가 정한 길이다(언리얼 `GRHISupportsPlacedResources` 와 같은 자리).
         */
        uint8 _bPlacedTextures{ SW_FALSE };
        /**
         * @brief 주 스왑체인이 서피스 회전(`currentTransform`)으로 만들어져 `IRHIDevice::getSurfaceTransform` 이 Identity 가 아닐 수 있는지입니다 — Android 의 Vulkan 만 참이다.
         * @details 렌더러는 이 능력이 있을 때만 회전 출력 패스의 PSO 를 만든다(`RenderPassTraitFlag::kSurfaceRotation`). 데스크톱은 PSO · 대기 텍스처 · 패스 모두 없다.
         */
        uint8 _bSurfacePreRotation{ SW_FALSE };

        /** @brief 기본값(컴퓨트만 켠 보수적 기본)으로 만듭니다. */
        RHICapabilities() noexcept = default;
    };
} // namespace sw
