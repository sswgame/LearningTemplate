/**
 * @file Engine/Renderer/Frame/Packet/RenderFrameSettings.cpp
 * @brief 렌더 설정 스위치의 전역 변수와, 그 값을 한 프레임 사본으로 뜨는 곳(게임 스레드)입니다.
 * @details 변수는 모두 익명 이름공간에 둔다 — 렌더 스레드 · 기록 워커가 도는 번역 단위는 이 이름을 볼 수 없다(헤더 머리말).
 *          콘솔 · 명령줄 · 사용자 설정은 이름(글)으로 변수를 찾으므로 바뀌지 않는다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Registry/RegistrationSlot.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    namespace
    {
        const RenderFrameSettings kDefaultSettings{};

        // --- 렌더 품질 · 성능 설정(그림은 같거나 품질만 다르다 — 배포에도 있다). 진단 · 비교 스위치는 개발 도구가 사본을 덮는다(`SW_RENDER_FRAME_SETTINGS_OVERRIDE`) ---

        /**
         * @brief `-gv_occlusionCulling=1`: GPU 컬링이 지난 프레임 장면 깊이로 가려진 인스턴스를 거릅니다(가림 타일). 컬링이 없는 백엔드(DX11)는 보지 않습니다.
         * @details 기본 끔 — 켜기 전에 무거운 장면에서 Release 로 재어 이득을 확인한다. 켜면 드러나는 물체가 한 프레임 늦게 나온다(지난 프레임 깊이로 본다).
         */
        SW_GLOBAL_VARIABLE( bool, gv_occlusionCulling, kDefaultSettings._bOcclusionCulling != SW_FALSE, "GPU 컬링의 가림 판정 — 지난 프레임 장면 깊이의 타일 최대 깊이 뒤의 인스턴스를 거른다 (컬링이 있는 백엔드)" );
        /** @brief `-gv_shadowCache=1`: 서 있는 점광 · 스폿 그림자의 정적 메시를 캐시 층에 한 번 그려 다시 씁니다(그림은 같다). */
        SW_GLOBAL_VARIABLE( bool, gv_shadowCache, kDefaultSettings._bShadowCache != SW_FALSE, "서 있는 점광 · 스폿 그림자의 정적 메시를 캐시 층에 한 번 그려 다시 쓴다 (정적 메시 MeshComponent::_bStatic 만, 그림은 같다)" );
        /**
         * @brief `-gv_shadowCascadeCulling=1`: 주 시점의 방향광 그림자를 캐스케이드마다 따로 컬링합니다(캐스케이드마다 컬링 슬롯 하나) — 같은 바이너리로 전/후를 비교합니다.
         * @details 0(기본)이면 모든 타일을 덮는 합친 상자 하나로 거른다. 이득은 `perf-compare --cases shadowCascadeCulling` 이 정한다.
         *          그림은 같고 비용만 다른 성능 설정이라 배포본에도 둔다(언리얼 `r.Shadow.*` cvar 와 같은 자리).
         */
        SW_GLOBAL_VARIABLE( bool, gv_shadowCascadeCulling, kDefaultSettings._bShadowCascadeCulling != SW_FALSE, "방향광 그림자를 캐스케이드마다 따로 컬링한다(0 이면 모든 타일을 덮는 상자 하나 — 전/후 비교용)" );
        /** @brief `-gv_upscalerSharpness=<단계>`: FSR 1 RCAS 의 선명도를 줄이는 단계입니다(0 = 가장 선명, 1 단계마다 절반 — AMD FSRRcasCon 의 sharpness). AMD 예제 기본값은 0.2 다. */
        SW_GLOBAL_VARIABLE( float32, gv_upscalerSharpness, kDefaultSettings._upscalerSharpness, "FSR 1 선명화(RCAS) 를 줄이는 단계 — 0 가장 선명, 1 마다 절반 (graphics.upscaler 가 spatial 일 때)" );

        // --- 사용자 설정(Config 의 사용자 설정 스키마가 이름으로 쓴다 — `UserSettingsVariables.h` 의 다른 사용자 설정과 같은 값 경로) ---

        SW_GLOBAL_VARIABLE( int32, gv_upscaler, kDefaultSettings._upscaler, "업스케일 필터 0 선형 1 FSR 1 — 렌더 배율 뷰를 Present 가 키울 때 (사용자 설정 graphics.upscaler)" );
        SW_GLOBAL_VARIABLE( int32, gv_shadowMapResolution, static_cast<int32>( kDefaultSettings._shadowMapResolution ), "그림자 맵 한 변 텍셀 수 (사용자 설정 graphics.shadowQuality 의 targetValue)" );
        SW_GLOBAL_VARIABLE( int32, gv_shadowFilter, kDefaultSettings._shadowFilter, "그림자 필터 0 PCF 3x3 1 PCSS — 원천 크기로 반그림자 폭을 바꾼다 (사용자 설정 graphics.shadowFilter)" );
        SW_GLOBAL_VARIABLE( int32, gv_postQuality, kDefaultSettings._postQuality, "후처리 품질 0~3 — 패스의 _minPostQuality 보다 낮으면 건너뛴다 (사용자 설정 graphics.postQuality)" );
        SW_GLOBAL_VARIABLE( bool, gv_motionBlur, kDefaultSettings._bMotionBlur != SW_FALSE, "모션 블러 (사용자 설정 graphics.motionBlur, MotionBlur 패스가 읽는다)" );
        /**
         * @brief `-gv_displayOutput=<SDR|HDR10|ScRGB>`: 화면 출력 장치입니다(사용자 설정 display.hdr). 백엔드 · 모니터가 받지 않으면 정보 로그 한 줄과 SDR 이다(`DisplayOutputDesc::resolve`).
         * @details 실행 중에 바꾸면 렌더 드라이버가 다음 프레임 앞에서 스왑체인을 다시 만든다(창 크기 바뀜과 같은 길 — `EngineRenderDriver::applyDisplayOutput`). 언리얼 `r.HDR.EnableHDROutput` · `r.HDR.Display.OutputDevice` 자리다.
         */
        SW_GLOBAL_VARIABLE( DisplayOutputKind, gv_displayOutput, kDefaultSettings._displayOutput._kind, "화면 출력 장치 SDR · HDR10 · ScRGB — 받지 않는 모니터는 SDR (사용자 설정 display.hdr)" );
        SW_GLOBAL_VARIABLE( float32, gv_displayPaperWhite, kDefaultSettings._displayOutput._paperWhiteNits, "HDR 출력의 종이 흰색(nits) — 장면 흰색 · UI 의 휘도 (사용자 설정 display.hdrPaperWhite)" );
        SW_GLOBAL_VARIABLE( float32, gv_displayMaxLuminance, kDefaultSettings._displayOutput._maxLuminanceNits, "HDR 출력의 최대 휘도(nits) — 톤 곡선이 닿는 휘도 (사용자 설정 display.hdrMaxLuminance)" );

        /** @brief 붙은 덮는 함수입니다(이미지에 하나 — 둘째는 거절). 걸기 · 떼기는 정적 초기화 · 종료 때, 부르기는 게임 스레드(`capture`)만. */
        SW_DEFINE_REGISTRATION_SLOT( getOverrideSlot, void( RenderFrameSettings& ), "RenderFrameSettingsOverride" )

        struct RenderFrameSettingsInternal
        {
            /// 그림자 맵 한 변의 상한 — 밉 상한을 넘는 한 변은 첨부를 만들지 못한다.
            static constexpr int32 kMaxShadowMapResolution = static_cast<int32>( 1u << ( constant::render::kMaxTextureMipCount - 1 ) );
            /// HDR 출력 휘도의 범위(nits) — `DisplayOutputDesc` 의 PROPERTY 범위와 같다(콘솔 · 명령줄 값도 이 안으로 자른다).
            static constexpr float32 kMinPaperWhiteNits   = 80.0f;
            static constexpr float32 kMaxPaperWhiteNits   = 500.0f;
            static constexpr float32 kMinMaxLuminanceNits = 100.0f;
            static constexpr float32 kMaxMaxLuminanceNits = 10000.0f;

            static uint8 toFlag( bool bValue ) { return bValue ? SW_TRUE : SW_FALSE; }
        };
    } // namespace

    RenderFrameSettings RenderFrameSettings::capture()
    {
        using Internal = RenderFrameSettingsInternal;
        // 엔진 서비스가 묶이기 전(렌더러 기동)은 게임 스레드 하나뿐이라 확인할 주인이 아직 없다.
        if ( engine::areEngineSubsystemsBound() )
            engine::getSubsystem<TaskManager>().ensureMainThread();
        RenderDiagnostics::publish(); // 패킷 밖 소비자(bindless 검사 · 셰이더 컴파일)의 원자 사본도 같은 자리에서 내보낸다
        RenderFrameSettings settings;
        settings._diagnostics                     = RenderDiagnostics::get();
        settings._displayOutput._kind             = gv_displayOutput;
        settings._displayOutput._paperWhiteNits   = MathUtil::clamp( static_cast<float32>( gv_displayPaperWhite ), Internal::kMinPaperWhiteNits, Internal::kMaxPaperWhiteNits );
        settings._displayOutput._maxLuminanceNits = MathUtil::clamp( static_cast<float32>( gv_displayMaxLuminance ), Internal::kMinMaxLuminanceNits, Internal::kMaxMaxLuminanceNits );
        settings._upscalerSharpness               = MathUtil::max( 0.0f, static_cast<float32>( gv_upscalerSharpness ) );
        settings._shadowMapResolution             = static_cast<uint32>( MathUtil::clamp( static_cast<int32>( gv_shadowMapResolution ), 1, Internal::kMaxShadowMapResolution ) );
        settings._postQuality                     = gv_postQuality;
        settings._upscaler                        = gv_upscaler;
        settings._shadowFilter                    = gv_shadowFilter;
        settings._bOcclusionCulling               = Internal::toFlag( gv_occlusionCulling );
        settings._bShadowCache                    = Internal::toFlag( gv_shadowCache );
        settings._bShadowCascadeCulling           = Internal::toFlag( gv_shadowCascadeCulling );
        settings._bMotionBlur                     = Internal::toFlag( gv_motionBlur );
        // 배포에는 덮는 쪽이 없다 — 진단 값은 늘 기본값(배포 동작)이다.
        if constexpr ( build::kShipping == false )
        {
            const RenderFrameSettingsOverride settingsOverride = getOverrideSlot().get();
            if ( settingsOverride != nullptr )
                settingsOverride( settings );
        }
        return settings;
    }

    bool RenderFrameSettings::registerOverride( RenderFrameSettingsOverride settingsOverride )
    {
        return getOverrideSlot().setLogged( settingsOverride, "Render frame settings override", "RenderFrameSettingsOverride" ) == RegistrationResult::Added;
    }

    void RenderFrameSettings::unregisterOverride( RenderFrameSettingsOverride settingsOverride )
    {
        (void)getOverrideSlot().reset( settingsOverride );
    }
} // namespace sw
