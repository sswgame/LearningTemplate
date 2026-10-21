/**
 * @file RHICapabilities.h
 * @brief RHI 백엔드 능력 플래그입니다. 백엔드마다의 값과 가용성 조회는 `RHIBackendInfo.h` 의 기술자 테이블에 있습니다.
 */
#pragma once
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @note 비트필드 대신 uint8 입니다. DLL 과 컴파일러 사이의 패킹, true 대입 문제를 피합니다. 0/1 만 씁니다.
     * @note **여기에 에디터 능력을 넣지 말 것.** "이 백엔드로 에디터가 도는가" 는
     *       디바이스가 아니라 **에디터가 아는 사실**입니다(ImGui 렌더러 백엔드가 있느냐). 그 답의 기준은
     *       `IImGuiRendererBackend::createRendererBackend` 하나이고, 없으면 nullptr 을 반환합니다.
     */
    struct SW_API RHICapabilities
    {
        uint8 _bBindless{ SW_FALSE };       ///< 디스크립터 인덱스 테이블 (드로우 시 바인드로 에뮬 가능)
        uint8 _bNativeBindless{ SW_FALSE }; ///< 하드웨어 디스크립터 인덱싱 / bindless 샘플링
        uint8 _bCompute{ SW_TRUE };         ///< 컴퓨트 셰이더
        uint8 _bOffscreenRT{ SW_FALSE };    ///< createTexture2D + 오프스크린 경로
        uint8 _bIndirectDraw{ SW_FALSE };   ///< drawIndirect / dispatchIndirect
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
         * @brief 삼각형을 선으로 채우는 래스터(`RHIFillMode::Wireframe`)를 쓸 수 있는지입니다.
         * @details Vulkan 은 디바이스 기능 `fillModeNonSolid` 가 있어야 한다. 없는 디바이스에서 와이어프레임 PSO 를 만들면 백엔드가 거절하고(솔리드로 바꾸지 않는다),
         *          렌더러는 와이어프레임 보기 모드를 받지 않는다(`FrameRenderer::setViewMode`).
         */
        uint8 _bWireframeFill{ SW_FALSE };

        /** @brief 기본값(컴퓨트만 켠 보수적 기본)으로 만듭니다. */
        RHICapabilities() noexcept = default;
    };
} // namespace sw
