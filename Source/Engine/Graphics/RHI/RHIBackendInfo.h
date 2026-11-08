/**
 * @file RHIBackendInfo.h
 * @brief RHI 백엔드마다 한 줄인 기술자 테이블(이름, 셰이더 타깃, 모듈, 플랫폼, 프로파일러 API, 능력)과 그 테이블을 읽는 가용성 조회입니다.
 * @details 백엔드를 더하면 이 테이블에 항목 하나를 더한다. 이름과 셰이더 타깃은 쿠킹 테이블(`Config/Engine/CookContract.json`)과 같아야 하고,
 *          `RHI.cpp` 가 생성 X-macro(`SW_RHI_BACKEND_TABLE`)로 항목마다 `static_assert` 한다.
 *          장치 모듈(`RHI_*`)도 이 헤더를 읽으므로 테이블은 `inline constexpr` 다(엔진 DLL 함수를 부르지 않는다).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Profiling/IProfilerGPUTimeline.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{
    /** @brief 백엔드 하나의 기술자입니다. 테이블(`RHIBackendInfoTable`)의 항목 하나입니다. */
    struct RHIBackendInfo
    {
        RHIBackend          _backend;
        const utf8*         _pName;        ///< 로그와 설정에 쓰는 이름(RHIBackend 열거자와 같다)
        ShaderTargetFormat  _shaderTarget; ///< 이 백엔드가 읽는 셰이더 바이트코드
        const utf8*         _pModuleName;  ///< 개발 빌드가 로드하는 장치 모듈 이름(`Bin/Modules/<이름>`)
        ProfilerGraphicsAPI _profilerAPI;  ///< 외부 프로파일러에 알리는 GPU API 종류
        RHICapabilities     _capabilities; ///< 정적 능력 테이블. 디바이스가 있으면 `IRHIDevice::getCapabilities` 가 런타임 값으로 덮을 수 있다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 백엔드마다의 정적 능력 테이블을 만듭니다. 기술자 테이블(`RHIBackendInfoTable`)의 능력 항목입니다.
     * @details 테이블과 다른 구조체에 두는 이유는, 클래스 안 정적 상수의 초기화에서는 같은 클래스의 constexpr 함수를 아직 부를 수 없기 때문입니다(클래스가 덜 정의됐습니다).
     */
    struct RHIStaticCapabilities
    {
        static constexpr RHICapabilities makeDirectX11Capabilities()
        {
            RHICapabilities caps{};
            caps._bBindless       = SW_TRUE;
            caps._bNativeBindless = SW_FALSE;
            caps._bCompute        = SW_TRUE;
            // D3D11 은 한 버퍼에 `BUFFER_STRUCTURED` 와 `DRAWINDIRECT_ARGS` 를 같이 설정할 수 없다.
            // gpu_cull.hlsl 이 간접 인자를 RWStructuredBuffer 로 쓰므로 그 버퍼를 인다이렉트 인자로도
            // 쓰려면 둘 중 하나를 포기해야 한다. 인다이렉트 드로우를 살리고 컬링을 끈다
            // (간접 인자는 GPUScene 이 CPU 에서 이미 채운다).
            caps._bGPUCulling        = SW_FALSE;
            caps._bGPUMeshMorph      = SW_TRUE; // 구조버퍼 SRV/UAV 만 쓴다. 간접 인자 제약과 무관하다
            caps._bMultiDrawIndirect = SW_TRUE;
            // **디바이스가 있으면 이 값을 믿지 말 것.** `D3D11RHIDevice::getCapabilities` 가
            // `D3D11_FEATURE_THREADING` 조회 결과로 이 항목을 덮어 **참이 될 수 있다**. 여기 FALSE 는
            // "드라이버를 모를 때의 보수적 기본값" 이지 "DX11 은 병렬로 기록하지 않는다" 가 아니다.
            // 둘을 혼동하면 병렬 테스트가 DX11 병렬 경로를 건너뛴다.
            caps._bParallelCommandRecording   = SW_FALSE;
            caps._bThreadSafeResourceCreation = SW_TRUE;
            caps._bWireframeFill              = SW_TRUE;
            caps._bMaterialTessellation       = SW_TRUE; // 헐 · 도메인이 정점 · 픽셀과 같은 상수 · 텍스처 · 구조 버퍼를 받는다(D3D11RHICommandContext 가 HS · DS 에도 건다)
            return caps;
        }

        static constexpr RHICapabilities makeDirectX12Capabilities()
        {
            RHICapabilities caps{};
            caps._bBindless                   = SW_TRUE;
            caps._bNativeBindless             = SW_TRUE; // 후보. 런타임은 Device::getCapabilities()
            caps._bCompute                    = SW_TRUE;
            caps._bGPUCulling                 = SW_TRUE;
            caps._bGPUMeshMorph               = SW_TRUE;
            caps._bMultiDrawIndirect          = SW_TRUE;
            caps._bParallelCommandRecording   = SW_TRUE;
            caps._bThreadSafeResourceCreation = SW_TRUE;
            caps._bWireframeFill              = SW_TRUE;
            caps._bMaterialTessellation       = SW_TRUE; // 루트 시그니처가 모든 스테이지에 보인다(D3D12_SHADER_VISIBILITY_ALL)
            return caps;
        }

        static constexpr RHICapabilities makeVulkanCapabilities()
        {
            RHICapabilities caps{};
            caps._bBindless       = SW_TRUE;
            caps._bNativeBindless = SW_TRUE; // 후보. 런타임은 supportsNativeBindlessSampling()
            caps._bCompute        = SW_TRUE;
            caps._bGPUCulling     = SW_TRUE;
            // 주의: 백엔드 하나가 다른 이미지를 내면 **쿠킹된 셰이더 산출물이 낡았는지부터 의심할 것**
            // (`App.exe --cook-shaders` 로 다시 쿠킹하고 비교한다). Vulkan 의 모프는 DX12, DX11 과 픽셀 수가 같다.
            caps._bGPUMeshMorph      = SW_TRUE;
            caps._bMultiDrawIndirect = SW_TRUE;
            // 리스트가 자기 VkCommandPool + VkCommandBuffer + 기록 상태를 소유한다.
            // 풀이 리스트마다 따로여야 하는 이유는 VkCommandPool 이 외부 동기화 대상이기
            // 때문이다. DX12 의 커맨드 얼로케이터와 같은 제약이다.
            caps._bParallelCommandRecording   = SW_TRUE;
            caps._bThreadSafeResourceCreation = SW_TRUE;
            caps._bWireframeFill              = SW_TRUE; // 후보. 런타임은 VulkanRHIDevice::getCapabilities 가 fillModeNonSolid 로 덮는다
            caps._bMaterialTessellation       = SW_TRUE; // 후보. 런타임은 VulkanRHIDevice::getCapabilities 가 tessellationShader 로 덮는다
            return caps;
        }

        /**
         * @brief WebGPU 의 능력입니다. bindless 는 DX11 · GL 과 같은 고정 슬롯 에뮬이고, 멀티 드로우 · 와이어프레임 · 병렬 기록이 없다(docs/PlatformTargets.md 의 표).
         * @details 장치는 만든 스레드에서만 쓴다 — 브라우저의 JS 객체는 워커를 넘지 못한다. GPU 컬링은 컴퓨트가 간접 인자를 저장 버퍼로 쓰고 같은 버퍼를 간접 드로우로 읽는다
         *          (WebGPU 는 한 버퍼에 Storage | Indirect 를 같이 준다 — DX11 이 컬링을 끈 까닭이 없다). 웨이브 연산은 쿠킹이 뺀다(`SW_NO_WAVE_OPS`).
         */
        static constexpr RHICapabilities makeWebGPUCapabilities()
        {
            RHICapabilities caps{};
            caps._bBindless                   = SW_TRUE;
            caps._bNativeBindless             = SW_FALSE;
            caps._bCompute                    = SW_TRUE;
            caps._bGPUCulling                 = SW_TRUE;
            caps._bGPUMeshMorph               = SW_TRUE;
            caps._bMultiDrawIndirect          = SW_FALSE;
            caps._bParallelCommandRecording   = SW_FALSE;
            caps._bThreadSafeResourceCreation = SW_FALSE;
            caps._bWireframeFill              = SW_FALSE;
            return caps;
        }

        static constexpr RHICapabilities makeOpenGlCapabilities()
        {
            RHICapabilities caps{};
            caps._bBindless       = SW_TRUE;
            caps._bNativeBindless = SW_FALSE;
            caps._bCompute        = SW_TRUE;
            caps._bGPUCulling     = SW_TRUE;
            // 주의: GL 드라이버는 early-return 형태의 `MorphPool::computeElement`(DXC 가 OpSwitch(0) 구조로 내는 코드)를
            // 잘못 컴파일해 풀에서 **한 슬롯 앞 원소**를 읽는다. 셰이더 쪽은 분기 없는 한 식이어야 한다(binding.hlsli 주석).
            // 회귀는 RenderPassGPUTest.MorphPoolIdentityMatchesRest 가 잡는다.
            caps._bGPUMeshMorph             = SW_TRUE;
            caps._bMultiDrawIndirect        = SW_TRUE;
            caps._bParallelCommandRecording = SW_FALSE;
#if defined( SW_PLATFORM_WINDOWS )
            caps._bRequiresWindowRecreate = SW_TRUE;
#endif
            caps._bThreadSafeResourceCreation = SW_FALSE;
            caps._bWireframeFill              = SW_TRUE;
            return caps;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 백엔드 기술자 테이블입니다. 항목 순서는 RHIBackend 열거값과 같습니다. */
    struct RHIBackendInfoTable
    {
        static constexpr RHIBackendInfo kArrInfo[] = {
            {RHIBackend::DirectX11, "DirectX11",   ShaderTargetFormat::DXBC_D3D11,   "RHI_DX11", ProfilerGraphicsAPI::Direct3D11, RHIStaticCapabilities::makeDirectX11Capabilities()},
            {RHIBackend::DirectX12, "DirectX12",   ShaderTargetFormat::DXIL_D3D12,   "RHI_DX12", ProfilerGraphicsAPI::Direct3D12, RHIStaticCapabilities::makeDirectX12Capabilities()},
            {   RHIBackend::Vulkan,    "Vulkan", ShaderTargetFormat::SPIRV_Vulkan, "RHI_Vulkan",     ProfilerGraphicsAPI::Vulkan,    RHIStaticCapabilities::makeVulkanCapabilities()},
            {   RHIBackend::OpenGL,    "OpenGL", ShaderTargetFormat::SPIRV_OpenGL,     "RHI_GL",     ProfilerGraphicsAPI::OpenGl,    RHIStaticCapabilities::makeOpenGlCapabilities()},
            {   RHIBackend::WebGPU,    "WebGPU",  ShaderTargetFormat::WGSL_WebGPU, "RHI_WebGPU",     ProfilerGraphicsAPI::WebGPU,    RHIStaticCapabilities::makeWebGPUCapabilities()},
        };

        /** @brief 그 백엔드의 항목입니다. 열거 범위 밖 값이면 nullptr 입니다. */
        static constexpr const RHIBackendInfo* find( RHIBackend backend )
        {
            const size_t index = static_cast<size_t>( backend );
            return index < std::size( kArrInfo ) ? &kArrInfo[index] : nullptr;
        }

        /** @brief 항목 순서가 열거값과 같은지 확인합니다(`find` 가 열거값을 인덱스로 쓴다). */
        static constexpr bool isOrderedByBackend()
        {
            for ( size_t index = 0; index < std::size( kArrInfo ); ++index )
            {
                if ( static_cast<size_t>( kArrInfo[index]._backend ) != index )
                    return false;
            }
            return true;
        }
    };
    static_assert( RHIBackendInfoTable::isOrderedByBackend(), "RHIBackendInfoTable rows must follow RHIBackend enumerator order" );
} // namespace sw

namespace sw
{
    /** @brief 이 OS와 빌드의 백엔드 가용성과 정적 능력 테이블 조회입니다. 둘 다 `RHIBackendInfoTable` 을 읽습니다. */
    struct RHIAvailability
    {
        /** @brief 이 플랫폼에서 만들 수 있는 백엔드입니다(건너갈 순서, 맨 앞이 기본). 원본은 `Config/Engine/CookContract.json` 의 `platform_rhi_backends` 입니다. */
        static span<const RHIBackend> getPlatformBackendOrder() noexcept
        {
#define SW_RHI_BACKEND_ORDER_ROW( Backend ) RHIBackend::Backend,
#if defined( SW_PLATFORM_WINDOWS )
            static constexpr RHIBackend kArrOrder[] = { SW_RHI_BACKEND_ORDER_Windows( SW_RHI_BACKEND_ORDER_ROW ) };
#elif defined( SW_PLATFORM_LINUX )
            static constexpr RHIBackend kArrOrder[] = { SW_RHI_BACKEND_ORDER_Linux( SW_RHI_BACKEND_ORDER_ROW ) };
#elif defined( SW_PLATFORM_ANDROID )
            static constexpr RHIBackend kArrOrder[] = { SW_RHI_BACKEND_ORDER_Android( SW_RHI_BACKEND_ORDER_ROW ) };
#elif defined( SW_PLATFORM_WEB )
            static constexpr RHIBackend kArrOrder[] = { SW_RHI_BACKEND_ORDER_Web( SW_RHI_BACKEND_ORDER_ROW ) };
#else
    #error "Unsupported platform"
#endif
#undef SW_RHI_BACKEND_ORDER_ROW
            return kArrOrder;
        }

        /**
         * @brief 검증 체인(`-scenario-backends=all`)이 이 플랫폼에서 묶음마다 도는 백엔드입니다(쿠킹 표 `scenario_backends` — 만들 수 있는 것의 부분집합).
         * @details 만들 수 있다고 모두 검증 체인에 드는 것은 아니다 — 골든을 갖춘 백엔드만 든다(언리얼 자동화도 플랫폼 기본 RHI 로 돌고 다른 RHI 는 따로 지정한다).
         */
        static span<const RHIBackend> getScenarioBackends() noexcept
        {
#define SW_RHI_SCENARIO_BACKEND_ROW( Backend ) RHIBackend::Backend,
#if defined( SW_PLATFORM_WINDOWS )
            static constexpr RHIBackend kArrScenario[] = { SW_RHI_SCENARIO_BACKENDS_Windows( SW_RHI_SCENARIO_BACKEND_ROW ) };
#elif defined( SW_PLATFORM_LINUX )
            static constexpr RHIBackend kArrScenario[] = { SW_RHI_SCENARIO_BACKENDS_Linux( SW_RHI_SCENARIO_BACKEND_ROW ) };
#elif defined( SW_PLATFORM_ANDROID )
            static constexpr RHIBackend kArrScenario[] = { SW_RHI_SCENARIO_BACKENDS_Android( SW_RHI_SCENARIO_BACKEND_ROW ) };
#elif defined( SW_PLATFORM_WEB )
            static constexpr RHIBackend kArrScenario[] = { SW_RHI_SCENARIO_BACKENDS_Web( SW_RHI_SCENARIO_BACKEND_ROW ) };
#else
    #error "Unsupported platform"
#endif
#undef SW_RHI_SCENARIO_BACKEND_ROW
            return kArrScenario;
        }

        /** @brief 이 OS/빌드에서 해당 백엔드를 만들 수 있으면 true 입니다. */
        static bool isAvailable( RHIBackend backend ) noexcept
        {
            for ( const RHIBackend candidate : getPlatformBackendOrder() )
            {
                if ( candidate == backend )
                    return true;
            }
            return false;
        }

        /** @brief 이 OS/빌드에서 @p current 말고 건너갈 백엔드입니다(플랫폼 순서에서 처음 되는 것). 없으면 false 입니다. */
        [[nodiscard]] static bool findOtherBackend( RHIBackend current, RHIBackend& outBackend ) noexcept
        {
            for ( const RHIBackend candidate : getPlatformBackendOrder() )
            {
                if ( candidate == current )
                    continue;
                outBackend = candidate;
                return true;
            }
            return false;
        }

        /** @brief 백엔드의 정적 capability 테이블을 반환합니다. 범위 밖 값이면 기본값(컴퓨트만)입니다. */
        static RHICapabilities query( RHIBackend backend ) noexcept
        {
            const RHIBackendInfo* pInfo = RHIBackendInfoTable::find( backend );
            return pInfo != nullptr ? pInfo->_capabilities : RHICapabilities{};
        }
    };
} // namespace sw
