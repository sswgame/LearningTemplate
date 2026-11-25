/**
 * @file Engine/Renderer/Frame/Packet/RenderFrameSettings.h
 * @brief 렌더 스레드 · 기록 워커가 한 프레임 동안 읽는 렌더 설정 스위치(개발 · 측정 · 사용자 품질)의 사본입니다.
 * @details 값의 원천은 전역 변수(`-gv_…` · 콘솔 · 에디터 · 사용자 설정)이고, 콘솔 · 에디터는 게임 스레드에서 잠금 없이 쓴다. 그래서 렌더 쪽은 변수를 직접 읽지 않는다 —
 *          게임 스레드가 패킷을 짓기 시작할 때 한 번 뜬 사본(`capture`)이 패킷에 실려 가고, 렌더러는 그 프레임 동안 그 사본만 본다(`FrameRenderState::_settings`).
 *          콘솔이 값을 바꾼 프레임의 패킷부터 새 값이 들고, 한 프레임 안에서는 패스 · 기록 워커가 모두 같은 값을 본다.
 *          언리얼 콘솔 변수의 렌더 스레드 사본(`GetValueOnRenderThread` — 게임 스레드 값을 렌더 명령으로 넘긴다)과 같은 생각이다.
 *          변수는 `RenderFrameSettings.cpp` 의 익명 이름공간에 있다 — 다른 번역 단위는 이름을 볼 수 없어 렌더 쪽 코드가 변수를 직접 읽는 모양을 쓸 수 없다.
 *          스위치를 더할 때는 이 구조체에 칸을 더하고 변수는 그 파일에 둔다. 패킷 밖 소비자(RHI 의 bindless 검사 · 셰이더 컴파일)는 `RenderDiagnostics` 원자 사본을 읽는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/Display/DisplayOutput.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"

namespace sw
{
    struct RenderFrameSettings;

    /** @brief 뜬 사본의 진단 · 비교 칸을 덮는 함수입니다(개발 도구의 `SW_RENDER_FRAME_SETTINGS_OVERRIDE`). 게임 스레드의 `RenderFrameSettings::capture` 만 부른다. */
    using RenderFrameSettingsOverride = void ( * )( RenderFrameSettings& inoutSettings );

    /**
     * @struct RenderFrameSettings
     * @brief 한 프레임의 렌더 설정 사본입니다. 기본값이 변수의 기본값(배포 동작)이다 — 패킷을 짓지 않는 경로(테스트)는 기본값으로 그린다.
     */
    struct RenderFrameSettings
    {
        RenderDiagnosticsPolicy _diagnostics{};                                   ///< 이 프레임의 진단 정책(`RenderDiagnostics::get` 의 그 프레임 값 — 라이트 그리드 생산자 · 안개 진단 · 메시지 예산)
        DisplayOutputDesc       _displayOutput{};                                 ///< 화면 출력 장치 값(`-gv_displayOutput` · 사용자 설정 display.hdr — 주 패킷은 렌더 드라이버가 능력에 맞춘 값으로 덮는다)
        float32                 _upscalerSharpness{ 0.2f };                       ///< FSR 1 RCAS 의 선명도를 줄이는 단계(0 = 가장 선명, 음수는 0 으로 자른 값 — `-gv_upscalerSharpness`)
        uint32                  _shadowMapResolution{ 2048 };                     ///< 그림자 맵 한 변의 텍셀 수(텍스처를 만들 수 있는 범위로 자른 값 — 사용자 설정 graphics.shadowQuality)
        int32                   _postQuality{ 2 };                                ///< 후처리 품질 0~3(패스의 `_minPostQuality` 보다 낮으면 건너뛴다 — 사용자 설정 graphics.postQuality)
        int32                   _upscaler{ 0 };                                   ///< 업스케일 필터(0 선형, 1 FSR 1 — 사용자 설정 graphics.upscaler)
        int32                   _shadowFilter{ 0 };                               ///< 그림자 필터 요청(0 PCF 3x3, 1 PCSS — 범위 밖은 `ShadowFilter::getActiveMode` 가 알리고 PCF 로 그린다)
        MeshMorphDiagMode       _meshMorphDebugMode{ MeshMorphDiagMode::Normal }; ///< 메시 모프 진단(`-gv_meshMorphDebugMode`)
        uint8                   _bGPUCulling{ SW_TRUE };                          ///< GPU 컬링 컴퓨트 디스패치(`-gv_gpuCulling`)
        uint8                   _bDrawMerge{ SW_TRUE };                           ///< 연속 배치를 멀티 드로우 하나로 묶기(`-gv_drawMerge`)
        uint8                   _bAliasTransients{ SW_TRUE };                     ///< 뷰 그래프 트랜지언트 첨부의 텍스처 나눔(`-gv_transientAliasing` — 콜백을 묶을 때 계획한다)
        uint8                   _bAsyncCompute{ SW_TRUE };                        ///< 프레임 패스의 비동기 컴퓨트 큐(`-gv_asyncCompute` — 파이프라인을 묶을 때 계획한다)
        uint8                   _bOcclusionCulling{ SW_FALSE };                   ///< GPU 컬링의 가림 판정(`-gv_occlusionCulling`)
        uint8                   _bVertexPool{ SW_TRUE };                          ///< 씬 메시 정점 풀(`-gv_vertexPool`)
        uint8                   _bShadowCache{ SW_FALSE };                        ///< 서 있는 점광 · 스폿 그림자의 정적 캐시 층(`-gv_shadowCache`)
        uint8                   _bShadowCascadeCulling{ SW_FALSE };               ///< 방향광 그림자의 캐스케이드마다 컬링(`-gv_shadowCascadeCulling`)
        uint8                   _bPSOAsync{ SW_TRUE };                            ///< 머티리얼 변형 PSO 를 빌더 스레드가 만든다(`-gv_psoAsync`)
        uint8                   _bMotionBlur{ SW_TRUE };                          ///< 모션 블러(사용자 설정 graphics.motionBlur)
        uint8                   _bRHIImmediateSubmit{ SW_FALSE };                 ///< 커맨드 리스트를 잘릴 때마다 바로 제출(`-gv_rhiImmediateSubmit`, 디버깅용)

        /**
         * @brief 지금의 변수 값으로 사본을 뜹니다. 게임 스레드 전용(Debug 단언)입니다 — 진단 정책도 여기서 내보낸다(`RenderDiagnostics::publish`).
         * @details 패킷을 짓기 시작할 때(`RenderPacketBuilder::beginPacket`)와 렌더러 기동(`FrameRendererSubsystem`) 때 부른다.
         */
        [[nodiscard]] SW_API static RenderFrameSettings capture();
        /** @brief 덮는 함수를 붙입니다. 이미 붙어 있으면 false 이고 오류를 남깁니다. */
        [[nodiscard]] SW_API static bool registerOverride( RenderFrameSettingsOverride settingsOverride );
        /** @brief @p settingsOverride 가 붙어 있으면 뗍니다. */
        SW_API static void unregisterOverride( RenderFrameSettingsOverride settingsOverride );
    };

    /** @brief `SW_RENDER_FRAME_SETTINGS_OVERRIDE` 가 두는 정적 등록자입니다(공통 틀 `StaticRegistrar`). */
    SW_DEFINE_STATIC_REGISTRAR( RenderFrameSettingsOverrideRegistrar, RenderFrameSettingsOverride, &RenderFrameSettings::registerOverride, &RenderFrameSettings::unregisterOverride );
} // namespace sw

/**
 * @brief 렌더 설정 사본을 덮는 함수를 붙입니다(개발 전용 .cpp 의 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다.
 * @details 진단 · 비교 스위치(컬링 끄기 · 배치 묶기 끄기 …)를 런타임 파일의 일반 변수로 두면 배포 바이너리가 그 스위치를 받는다 — 변수는 개발 도구에만 있고 런타임은 이 창구만 둔다.
 */
#define SW_RENDER_FRAME_SETTINGS_OVERRIDE( id, settingsOverride )                       \
    static const ::sw::RenderFrameSettingsOverrideRegistrar sw_renderFrameSettings_##id \
    {                                                                                   \
        settingsOverride                                                                \
    }
