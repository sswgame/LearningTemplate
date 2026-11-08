/**
 * @file Engine/Graphics/Debug/RenderDiagnostics.h
 * @brief 렌더러 진단 정책입니다 — 진단 패스(셰이더 assert · NaN 감시) · 셰이더 컴파일 옵션 · 검증 수준 · 비교용 생산자 · 진단 보기를 값 하나(`RenderDiagnosticsPolicy`)로 묻는 창구입니다.
 * @details 런타임(RHI · 셰이더 캐시 · 렌더러)은 `RenderDiagnostics::get` 만 묻고 누가 값을 채우는지 모른다. 개발 도구가 `SW_RENDER_DIAGNOSTICS_PROVIDER` 한 줄로
 *          채우는 함수를 붙이고(실행 인자 · 콘솔 · 에디터 창이 바꾸는 개발 변수를 읽는다), 아무것도 붙지 않으면 기본값(진단 없음, GPU 라이트 그리드)이다.
 *          Shipping 은 `get` 이 상수 기본값이라 진단 분기가 접혀 바이너리에서 빠진다. 진단마다 setter · 변수를 늘리지 않고 이 구조체에 칸을 더한다.
 *          언리얼은 같은 자리를 `r.ShaderDevelopmentMode` · `r.CheckNaN` · `r.Shaders.Optimize` 같은 개발 콘솔 변수로 두고 Shipping 에서 컴파일 아웃한다 —
 *          여기서는 변수는 개발 도구에 두고 런타임에는 정책 값 하나만 남긴다(Godot 의 RenderingServer 디버그 설정 묶음과 같은 모양).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum LightGridProducer
     * @brief 라이트 그리드를 누가 만드는지입니다. 진단 정책이 고르고(개발 도구 `-gv_lightCulling`) 기본은 GPU 입니다.
     * @details 리플렉션 enum 이라 명령줄이 이름으로 고른다(`-gv_lightCulling=CPU`). 숫자는 받지 않는다.
     */
    ENUM()
    enum class LightGridProducer : uint8
    {
        None, ///< 그리드 없이 목록 전체를 돈다(비교 기준)
        CPU,  ///< `LightGrid::populate` 가 만들어 동적 버퍼로 올린다
        GPU,  ///< 컴퓨트 light_grid.hlsl 이 기본 힙 버퍼에 만든다(`FramePassExecutor::dispatchLightGrid`)
    };

    /**
     * @brief 안개 패스의 진단 보기입니다. 저작 값은 바꾸지 않는다 — 같은 프레임 조건에서 그림만 다르게 낸다.
     * @details 언리얼의 Buffer Visualization · 분할 비교와 같은 자리다. 진단 보기는 볼륨이 켜진 주 시점에서만 뜻이 있고, 꺼졌으면 단면은 검정이다(이유는 `FogVolumeState`).
     */
    ENUM()
    enum class FogDebugView : uint8
    {
        None,          ///< 보통 그림
        Compare,       ///< 화면 왼쪽 반은 볼륨 안개, 오른쪽 반은 평범한 안개(같은 프레임 · 같은 설정)
        Scattering,    ///< froxel 단면(`RenderDiagnosticsPolicy::_fogDebugSlice` 번째 거리 조각까지 누적한 산란 rgb)을 화면 칸 그대로
        Transmittance, ///< 같은 단면의 투과율(흰색 1 = 안개 없음)
    };

    /**
     * @enum BindlessIndexCheck
     * @brief 셰이더에 넘기기 직전의 bindless 인덱스 검사 수준입니다(`BindlessIndexValidator`). 뒤의 수준은 앞의 것을 포함한다.
     */
    ENUM()
    enum class BindlessIndexCheck : uint8
    {
        Off,         ///< 검사하지 않는다
        DeadIndex,   ///< 죽은 번호를 오류로 남긴다
        TraceAbsent, ///< 죽은 번호와 "없음"(kInvalidDescriptorIndex)이 셰이더로 가는 자리(정보)까지 남긴다
    };

    /**
     * @struct RenderDiagnosticsPolicy
     * @brief 이 실행의 렌더러 진단 값입니다. 기본값이 배포 동작이다. 묻는 쪽마다 필요한 칸만 읽는다.
     * @details 셰이더 메시지는 단계를 가리지 않는 스위치다 — 어느 패스가 메시지 변형을 갖는지는 패스 표(`RenderPassTraitFlag::kGPUMessageTarget`)가 정한다.
     */
    struct RenderDiagnosticsPolicy
    {
        uint8              _bGPUMessages{ SW_FALSE };                      ///< 셰이더 메시지(`SW_GPU_PRINT` · `SW_GPU_LOG` · `SW_GPU_ASSERT`) 변형으로 컴파일하고 링 버퍼를 건다(`GPUMessageChannel`) — 렌더러 초기화 때 읽는다
        uint8              _gpuMessageLogBudget{ 4 };                      ///< `SW_GPU_LOG` 가 메시지 id 하나당 한 프레임에 적는 기록 수(`GPUMessageChannel::updateControl`)
        uint8              _bNanCheck{ SW_FALSE };                         ///< 패스 출력의 NaN · Inf 를 컴퓨트로 센다(`GPUNanCheck`, 진단 패스 `RenderPassType::NanCheck`) — 렌더러 초기화 때 읽는다
        uint8              _bShaderDebugCodegen{ SW_FALSE };               ///< 실행 중 셰이더 컴파일을 디버그 코드젠으로(RenderDoc 에서 한 줄씩, `ShaderCache::makeLiveCompileDesc`)
        BindlessIndexCheck _bindlessIndexCheck{ BindlessIndexCheck::Off }; ///< bindless 인덱스 검사 수준(`BindlessIndexValidator`)
        LightGridProducer  _lightGridProducer{ LightGridProducer::GPU };   ///< 라이트 그리드 생산자(`FrameRenderer::selectLightGridProducer`)
        FogDebugView       _fogDebugView{ FogDebugView::None };            ///< 안개 패스의 진단 보기(`FrameFog::makeDebugParams`)
        uint8              _fogDebugSlice{ 16 };                           ///< 진단 단면의 거리 조각(0 .. `SW_FOG_VOLUME_DIM_Z` - 1, 큰 번호가 먼 조각 — 범위 밖은 읽는 쪽이 자른다)
    };

    /** @brief 정책을 채우는 함수입니다. 기본값으로 만든 @p inoutPolicy 에서 바꿀 칸만 씁니다. 어느 스레드에서든 불릴 수 있다(렌더 스레드 · 기록 워커 · 셰이더 컴파일). */
    using RenderDiagnosticsProvider = void ( * )( RenderDiagnosticsPolicy& inoutPolicy );

    /**
     * @class RenderDiagnostics
     * @brief 진단 정책의 창구입니다. 채우는 쪽은 이미지에 하나만 붙는다 — 둘째 등록은 오류로 거절한다(어느 쪽 값인지 모르게 섞지 않는다).
     */
    class SW_API RenderDiagnostics
    {
    public:
        /** @brief 지금의 진단 정책입니다. 채우는 쪽이 없으면 기본값이고, Shipping 은 늘 기본값(상수)입니다. 값은 부를 때마다 새로 읽는다(실행 중에 바뀐 개발 변수가 바로 든다). */
        [[nodiscard]] static RenderDiagnosticsPolicy get()
        {
            // 전처리기로 접는다 — `BuildInfo.h` 는 CMake 정의를 요구해 리플렉션 파서가 이 헤더를 읽지 못한다(SW_SHIPPING 은 구성 정의 그대로다).
#if defined( SW_SHIPPING )
            return RenderDiagnosticsPolicy{};
#else
            return fetch();
#endif
        }

        /** @brief 채우는 함수를 붙입니다. 이미 붙어 있으면 false 이고 오류를 남깁니다. */
        [[nodiscard]] static bool registerProvider( RenderDiagnosticsProvider provider );
        /** @brief @p provider 가 붙어 있으면 뗍니다. */
        static void unregisterProvider( RenderDiagnosticsProvider provider );

    private:
        static RenderDiagnosticsPolicy fetch();
    };

    /** @brief `SW_RENDER_DIAGNOSTICS_PROVIDER` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 거절된 등록은 떼지 않는다). */
    SW_DEFINE_STATIC_REGISTRAR( RenderDiagnosticsRegistrar, RenderDiagnosticsProvider, &RenderDiagnostics::registerProvider, &RenderDiagnostics::unregisterProvider );
} // namespace sw

/** @brief 진단 정책을 채우는 함수를 붙입니다(.cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다. */
#define SW_RENDER_DIAGNOSTICS_PROVIDER( id, provider )                      \
    static const ::sw::RenderDiagnosticsRegistrar sw_renderDiagnostics_##id \
    {                                                                       \
        provider                                                            \
    }
