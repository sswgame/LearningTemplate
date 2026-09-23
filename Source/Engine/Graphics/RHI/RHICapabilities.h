/**
 * @file RHICapabilities.h
 * @brief RHI 백엔드 능력과 OS · 빌드 가용성 조회입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) RHICapabilities — bindless / 컴퓨트 / 인디렉트
    // ------------------------------------------------------------------------------
    /**
     * @note 비트필드 대신 uint8 입니다. DLL · 컴파일러 간 패킹 · true 대입 문제를 피합니다. 0/1 만 씁니다.
     * @note **여기에 에디터 능력을 다시 넣지 말 것.** 예전에 `_bEditorSupported` · `_bImGuiHooks` 가 있었는데
     *       네 백엔드가 모두 1 이라 검사하는 쪽이 모두 죽은 분기였고, 무엇보다 "이 백엔드로 에디터가 도는가" 는
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
        uint8 _bGpuCulling{ SW_FALSE };     ///< 컴퓨트 컬 + 인디렉트 인자 경로
        /**
         * @brief GPU 메시 모프입니다. 컴퓨트가 정점을 변형하고 정점 셰이더가 그 결과를 풀링합니다.
         * @details 끄면 모프를 요청한 메시도 **레스트 포즈로 그려집니다**(셰이더의 폴백 경로 그대로).
         *          언리얼도 스킨 캐시를 못 쓰면 일반 정점 팩토리로 되돌립니다. 그리기가 멈추지는 않습니다.
         */
        uint8 _bGpuMeshMorph{ SW_FALSE };
        uint8 _bMultiDrawIndirect{ SW_FALSE };        ///< 멀티 드로우 / count 버퍼 (DX12/VK/GL; DX11은 루프)
        uint8 _bParallelCommandRecording{ SW_FALSE }; ///< 멀티스레드 커맨드 리스트 병렬 기록 및 제출 지원 (DX12/VK)
        uint8 _bRequiresWindowRecreate{ SW_FALSE };   ///< OS 윈도우 픽셀 포맷 1회 제한(Windows WGL 등)으로 핫스왑 시 윈도우 재생성 필요
        /**
         * @brief 워커 스레드에서 GPU 리소스를 **만들어도** 되는지입니다(그리기가 아니라 생성만).
         * @details DX12 · Vulkan 은 디바이스 레벨 생성이 스펙상 스레드 안전하고, DX11 도 ID3D11Device 는
         *          (컨텍스트와 달리) 안전합니다. OpenGL 은 `glGen*` 이 **현재 컨텍스트**를 필요로 해 안 됩니다.
         *          `GpuUploadQueue` 가 이 값으로 워커 병렬과 인라인을 가릅니다.
         */
        uint8 _bThreadSafeResourceCreation{ SW_FALSE };

        /** @brief 기본값(컴퓨트만 켠 보수적 기본)으로 만듭니다. */
        RHICapabilities() noexcept = default;
    };

    // ------------------------------------------------------------------------------
    // 2) RHIAvailability — OS/빌드에서 생성 가능 여부 + 백엔드별 능력표
    // ------------------------------------------------------------------------------
    struct RHIAvailability
    {
        /** @brief 이 OS/빌드에서 해당 백엔드를 만들 수 있으면 true. */
        static bool isAvailable( RHIBackend backend ) noexcept
        {
            // DX11 · DX12 가 같은 답을 내는 것은 의도다. 둘 다 Windows 전용이다.
            switch ( backend )
            {
                // NOLINTNEXTLINE(bugprone-branch-clone)
                case RHIBackend::DirectX11:
                case RHIBackend::DirectX12:
#if defined( SW_PLATFORM_WINDOWS )
                    return true;
#else
                    return false;
#endif
                case RHIBackend::Vulkan:
                case RHIBackend::OpenGL:
                    return true;
                default:
                    break;
            }
            return false;
        }

        /** @brief 백엔드의 정적 capability 표를 반환합니다. */
        static RHICapabilities query( RHIBackend backend ) noexcept
        {
            RHICapabilities caps{};
            switch ( backend )
            {
                case RHIBackend::DirectX12:
                {
                    caps._bBindless                   = SW_TRUE;
                    caps._bNativeBindless             = SW_TRUE; // 후보. 런타임은 Device::getCapabilities()
                    caps._bCompute                    = SW_TRUE;
                    caps._bOffscreenRT                = SW_TRUE;
                    caps._bIndirectDraw               = SW_TRUE;
                    caps._bGpuCulling                 = SW_TRUE;
                    caps._bGpuMeshMorph               = SW_TRUE;
                    caps._bMultiDrawIndirect          = SW_TRUE;
                    caps._bParallelCommandRecording   = SW_TRUE;
                    caps._bThreadSafeResourceCreation = SW_TRUE;
                    break;
                }
                case RHIBackend::DirectX11:
                {
                    caps._bBindless       = SW_TRUE;
                    caps._bNativeBindless = SW_FALSE;
                    caps._bCompute        = SW_TRUE;
                    caps._bOffscreenRT    = SW_TRUE;
                    caps._bIndirectDraw   = SW_TRUE;
                    // D3D11 은 한 버퍼에 `BUFFER_STRUCTURED` 와 `DRAWINDIRECT_ARGS` 를 같이 걸 수 없다.
                    // gpucull.hlsl 이 간접 인자를 RWStructuredBuffer 로 쓰므로 그 버퍼를 인다이렉트 인자로도
                    // 쓰려면 둘 중 하나를 포기해야 한다. 인다이렉트 드로우를 살리고 컬링을 끈다
                    // (간접 인자는 GpuScene 이 CPU 에서 이미 채운다).
                    caps._bGpuCulling        = SW_FALSE;
                    caps._bGpuMeshMorph      = SW_TRUE; // 구조버퍼 SRV/UAV 만 쓴다. 간접 인자 제약과 무관하다
                    caps._bMultiDrawIndirect = SW_TRUE;
                    // **디바이스가 있으면 이 값을 믿지 말 것.** `D3D11RHIDevice::getCapabilities` 가
                    // `D3D11_FEATURE_THREADING` 조회 결과로 이 항목을 덮어 **참이 될 수 있다**. 여기 FALSE 는
                    // "드라이버를 모를 때의 보수적 기본값" 이지 "DX11 은 병렬로 기록하지 않는다" 가 아니다.
                    // 그 둘을 혼동해 병렬 테스트가 DX12 만 돌았고, DX11 병렬 경로의 레이스 둘이 오래 살았다.
                    caps._bParallelCommandRecording   = SW_FALSE;
                    caps._bThreadSafeResourceCreation = SW_TRUE;
                    break;
                }
                case RHIBackend::OpenGL:
                    caps._bBindless       = SW_TRUE;
                    caps._bNativeBindless = SW_FALSE;
                    caps._bCompute        = SW_TRUE;
                    caps._bOffscreenRT    = SW_TRUE;
                    caps._bIndirectDraw   = SW_TRUE;
                    caps._bGpuCulling     = SW_TRUE;
                    // 오래 꺼져 있었다. GL 만 정점 셰이더가 풀에서 **한 칸 앞 원소**를 읽었다. 엔진이 준 바이트는
                    // 모두 되읽어 맞았고, 원인은 드라이버가 early-return 모양의 `SwMorphElementOf` (DXC 가
                    // OpSwitch(0) 구조로 내는 코드) 를 잘못 컴파일한 것이었다. 분기 없는 한 식으로 바꾸자 네
                    // 백엔드가 같다(binding.hlsli 주석). 회귀는 RenderPassGpuTest.MorphPoolIdentityMatchesRest 가 잡는다.
                    caps._bGpuMeshMorph             = SW_TRUE;
                    caps._bMultiDrawIndirect        = SW_TRUE;
                    caps._bParallelCommandRecording = SW_FALSE;
#if defined( SW_PLATFORM_WINDOWS )
                    caps._bRequiresWindowRecreate = SW_TRUE;
#endif
                    caps._bThreadSafeResourceCreation = SW_FALSE;
                    break;
                case RHIBackend::Vulkan:
                {
                    caps._bBindless       = SW_TRUE;
                    caps._bNativeBindless = SW_TRUE; // 후보. 런타임은 supportsNativeBindlessSampling()
                    caps._bCompute        = SW_TRUE;
                    caps._bOffscreenRT    = SW_TRUE;
                    caps._bIndirectDraw   = SW_TRUE;
                    caps._bGpuCulling     = SW_TRUE;
                    // 한때 "Vulkan 도 GL 과 같이 깨졌다" 고 적었는데 **그것은 구운 셰이더가 낡았던 것**이다
                    // (`forwardlit` 바이너리가 라이트 버퍼 이전 것이었다). 베이크 신선도 판정을 파일
                    // 시간에서 내용 해시로 바꾼 뒤 다시 재니 DX12 · DX11 과 픽셀 수가 같다. 백엔드 하나가
                    // 다른 그림을 낼 때 **셰이더 산출물부터 의심할 것**(백로그 1-4 참고).
                    caps._bGpuMeshMorph      = SW_TRUE;
                    caps._bMultiDrawIndirect = SW_TRUE;
                    // 리스트가 자기 VkCommandPool + VkCommandBuffer + 기록 상태를 소유한다(S4).
                    // 풀이 리스트마다 따로여야 하는 이유는 VkCommandPool 이 외부 동기화 대상이기
                    // 때문이다. DX12 의 커맨드 얼로케이터와 같은 제약이다.
                    caps._bParallelCommandRecording   = SW_TRUE;
                    caps._bThreadSafeResourceCreation = SW_TRUE;
                    break;
                }
                default:
                    break;
            }
            return caps;
        }
    };
} // namespace sw
