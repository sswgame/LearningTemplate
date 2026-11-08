/**
 * @file DevTools/Client/Render/RenderDiagnosticsSwitches.cpp
 * @brief 렌더러 진단 정책(`RenderDiagnosticsPolicy`)을 채우는 개발 변수들입니다 — 실행 인자 · 콘솔 · 에디터 창(볼륨 안개)이 바꾸고, 렌더러는 `RenderDiagnostics::get` 으로만 묻는다.
 * @details 예전에는 이 변수들이 런타임 파일(GPUAssert · GPUNanCheck · FrameRenderer · FrameFog · BindlessIndexValidator · ShaderCache)에 흩어져 배포 바이너리에 이름이 남았다.
 *          이름은 그대로라 시나리오 · 스크립트의 `-gv_…` 는 바뀌지 않는다. 값은 부를 때마다 새로 읽는다(실행 중에 바꾼 값이 다음 질의부터 든다) —
 *          셰이더 assert · NaN 감시는 렌더러 초기화 때 PSO · 버퍼를 정하므로 명령줄로 준다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"

#include <limits>

namespace sw
{
    namespace
    {
        /** @brief Debug 는 늘 죽은 번호를 검사하고, Release 는 성능 측정에 쓰는 빌드라 끈다(필요할 때 켠다). 선언을 하나로 두어 도구(launch-args)가 한 변수로 읽는다. */
        constexpr BindlessIndexCheck kDefaultBindlessIndexCheck =
#if defined( SW_DEBUG )
            BindlessIndexCheck::DeadIndex;
#else
            BindlessIndexCheck::Off;
#endif
    } // namespace

    /**
     * @brief `-gv_gpuMessages=1`: 메시지를 가진 셰이더(`RenderPassTraitFlag::kGPUMessageTarget`)를 `SW_GPU_MESSAGES_ENABLED` 변형으로 컴파일하고 링 버퍼를 건다 — assert · 출력 · 로그를 CPU 가 몇 프레임 뒤 로그로 낸다.
     * @details 예전 이름은 `-gv_gpuAssert` 였다(assert 만 실었다). 출력(`SW_GPU_PRINT`)은 `gpu.inspect x y` · `gpu.thread n` 이 고른 픽셀 · 스레드만 적는다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_gpuMessages, false, "셰이더 메시지(SW_GPU_PRINT · SW_GPU_LOG · SW_GPU_ASSERT) 를 켠다 - 셰이더가 UAV 링에 기록하고 CPU 가 몇 프레임 뒤 로그로 낸다" );
    /** @brief `-gv_gpuMessageLogBudget=N`: `SW_GPU_LOG` 가 메시지 id 하나당 한 프레임에 적는 기록 수(언리얼 ShaderPrint 의 최대 글자 수와 같은 상한). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_gpuMessageLogBudget, RenderDiagnosticsPolicy{}._gpuMessageLogBudget, "SW_GPU_LOG 가 메시지 id 하나당 한 프레임에 적는 기록 수 (0 .. 255)" );
    /** @brief `-gv_nanCheck=1`: 그래프가 끝난 뒤 주 시점의 부동소수 패스 출력을 컴퓨트로 훑어 NaN · Inf 를 패스 이름과 함께 로그로 낸다(언리얼 r.CheckNaN, DX12 · Vulkan 만). */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_nanCheck, false, "패스 출력 텍스처의 NaN/Inf 를 컴퓨트로 세고 패스 이름과 함께 로그로 낸다 (DX12 · Vulkan 만)" );
    /** @brief `-gv_shaderDebugCodegen=1`: 실행 중 셰이더 컴파일을 디버그 코드젠으로(RenderDoc 에서 한 줄씩, 언리얼 `r.Shaders.Optimize=0`). 기동 때 컴파일한 셰이더도 따르므로 명령줄로 준다. */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_shaderDebugCodegen, false, "실행 중 셰이더 컴파일을 디버그 코드젠으로(RenderDoc 에서 한 줄씩) — 기본은 쿠킹과 같은 최적화 코드젠" );
    /**
     * @brief `-gv_validateBindlessIndex=<수준>`: 셰이더에 넘기기 직전의 bindless 인덱스 검사(Graphics README "함정과 주의").
     * @details Debug 는 늘 죽은 번호를 검사한다. Release 는 성능 측정에 쓰는 빌드라(검사는 드로우마다 읽기 잠금 하나) 끄고 필요할 때 켠다.
     *          TraceAbsent 는 "없음" 번호가 셰이더로 가는 자리도 남긴다 — 드라이버가 디스크립터 읽기를 분기 앞으로 끌어올려 디바이스를 잃었을 때 후보를 고른다.
     */
    SW_TEST_GLOBAL_VARIABLE( BindlessIndexCheck, gv_validateBindlessIndex, kDefaultBindlessIndexCheck,
                             "셰이더에 넘기는 bindless 인덱스 검사 (Off / DeadIndex=죽은 번호, Debug 기본 / TraceAbsent='없음' 번호가 가는 자리까지)" );
    /** @brief `-gv_lightCulling=<생산자>`: 라이트 그리드 생산자 — None 은 그리드 없이 목록 전체(비교 기준), CPU 는 CPU 클러스터 그리드, GPU(기본 · 배포)는 컴퓨트 그리드. 학습용으로 셋을 나란히 본다. */
    SW_TEST_GLOBAL_VARIABLE( LightGridProducer, gv_lightCulling, LightGridProducer::GPU, "라이트 컬링 (None=그리드 없이 목록 전체, CPU=CPU 클러스터 그리드, GPU=GPU 클러스터 그리드)" );
    /** @brief `-gv_fogDebugView=<보기>`: 안개 패스의 진단 보기. 에디터의 볼륨 안개 창이 이 이름으로 고른다. */
    SW_TEST_GLOBAL_VARIABLE( FogDebugView, gv_fogDebugView, FogDebugView::None, "안개 진단 보기 (None / Compare=왼쪽 볼륨 · 오른쪽 평범 / Scattering / Transmittance=froxel 단면)" );
    /** @brief `-gv_fogDebugSlice=N`: 진단 단면의 거리 조각(큰 번호가 먼 조각, 그 조각까지 누적한 값). 범위 밖은 렌더러가 froxel 조각 수로 자른다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_fogDebugSlice, RenderDiagnosticsPolicy{}._fogDebugSlice, "안개 진단 단면의 거리 조각 (0 .. 31, 큰 번호가 먼 조각)" );

    namespace
    {
        struct RenderDiagnosticsSwitchesInternal
        {
            /// 정책의 uint8 칸(안개 단면 · 메시지 예산)에 담을 수 있는 범위입니다 — froxel 조각 수로 자르는 일은 렌더러가 한다.
            static constexpr int32 kUint8FieldMin = 0;
            static constexpr int32 kUint8FieldMax = static_cast<int32>( std::numeric_limits<uint8>::max() );

            static void fill( RenderDiagnosticsPolicy& inoutPolicy )
            {
                inoutPolicy._bGPUMessages        = gv_gpuMessages ? SW_TRUE : SW_FALSE;
                inoutPolicy._gpuMessageLogBudget = static_cast<uint8>( MathUtil::clamp( gv_gpuMessageLogBudget, kUint8FieldMin, kUint8FieldMax ) );
                inoutPolicy._bNanCheck           = gv_nanCheck ? SW_TRUE : SW_FALSE;
                inoutPolicy._bShaderDebugCodegen = gv_shaderDebugCodegen ? SW_TRUE : SW_FALSE;
                inoutPolicy._bindlessIndexCheck  = gv_validateBindlessIndex;
                inoutPolicy._lightGridProducer   = gv_lightCulling;
                inoutPolicy._fogDebugView        = gv_fogDebugView;
                inoutPolicy._fogDebugSlice       = static_cast<uint8>( MathUtil::clamp( gv_fogDebugSlice, kUint8FieldMin, kUint8FieldMax ) );
            }
        };
    } // namespace

    SW_RENDER_DIAGNOSTICS_PROVIDER( DevToolsSwitches, &RenderDiagnosticsSwitchesInternal::fill );
} // namespace sw
