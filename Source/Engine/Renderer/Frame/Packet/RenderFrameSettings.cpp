/**
 * @file Engine/Renderer/Frame/Packet/RenderFrameSettings.cpp
 * @brief 렌더 설정 스위치의 전역 변수와, 그 값을 한 프레임 사본으로 뜨는 곳(게임 스레드)입니다.
 * @details 변수는 모두 익명 이름공간에 둔다 — 렌더 스레드 · 기록 워커가 도는 번역 단위는 이 이름을 볼 수 없다(헤더 머리말).
 *          콘솔 · 명령줄 · 사용자 설정은 이름(글)으로 변수를 찾으므로 바뀌지 않는다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    namespace
    {
        const RenderFrameSettings kDefaultSettings{};

        // --- 렌더 개발 · 측정 스위치 ---

        /**
         * @brief `-gv_gpuCulling=0` 이면 GPU 컬링 컴퓨트 디스패치를 건너뜁니다(인다이렉트 드로우는 그대로 실행합니다).
         * @details 간접 인자는 GPUScene 이 CPU 에서 이미 채워 두므로, 이 디스패치만 빼면 "컴퓨트가 인자를 망치는가" 를 백엔드별로 가를 수 있다. 기본은 켬이다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_gpuCulling, kDefaultSettings._bGPUCulling != SW_FALSE, "GPU 컬링 컴퓨트 디스패치 (false=건너뜀, 진단용)" );
        /**
         * @brief `-gv_meshMorphDebugMode=<모드>`: 메시 모프 진단입니다. SkipDispatch 의 정답은 **레스트 포즈와 같은 이미지**다 — 컴퓨트가 아예 돌지 않으므로 "컴퓨트가 범인인가" 를 가른다.
         */
        SW_GLOBAL_VARIABLE( MeshMorphDiagMode, gv_meshMorphDebugMode, kDefaultSettings._meshMorphDebugMode, "메시 모프 진단 (Normal 평소 / ForceOn 강제 켬 / SkipDispatch 디스패치 생략)" );
        /**
         * @brief `-gv_drawMerge=0` 이면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶지 않고 배치마다 한 번씩 부릅니다.
         * @details 묶은 이미지와 묶지 않은 이미지가 같아야 한다. 다르면 배치 테이블(g_SwBatches)이나 드로우 ID 가 틀린 것이다. 기본은 묶는 것이다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_drawMerge, kDefaultSettings._bDrawMerge != SW_FALSE, "씬 배치 멀티 드로우 묶기 (false=배치마다 호출, 진단용)" );
        /**
         * @brief `-gv_asyncCompute=0` 이면 `_queue="AsyncCompute"` 프레임 패스도 그래픽스 큐에서 돕니다(시작 인자 — 파이프라인을 묶을 때 계획한다).
         * @details 비동기 큐가 이득인지는 측정으로 정한다(같은 장면에서 켜고 끈 GPU 프레임 시간). 능력이 없는 백엔드는 이 값과 상관없이 그래픽스 큐다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_asyncCompute, kDefaultSettings._bAsyncCompute != SW_FALSE, "프레임 패스의 비동기 컴퓨트 큐 (false=모두 그래픽스 큐, 측정용)" );
        /**
         * @brief `-gv_transientAliasing=0` 이면 뷰 그래프의 트랜지언트 첨부가 텍스처를 나누지 않습니다(시작 인자 — 콜백을 묶을 때 계획한다).
         * @details 나눈 그림과 나누지 않은 그림이 같아야 한다 — 다르면 계획이 겹치는 수명을 놓쳤다. 메모리는 `RT.Frame.transientAliases` 가 센다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_transientAliasing, kDefaultSettings._bAliasTransients != SW_FALSE, "뷰 그래프 트랜지언트 첨부의 텍스처 나눔 (false=첨부마다 텍스처, 진단용)" );
        /**
         * @brief `-gv_occlusionCulling=1`: GPU 컬링이 지난 프레임 장면 깊이로 가려진 인스턴스를 거릅니다(가림 타일). 컬링이 없는 백엔드(DX11)는 보지 않습니다.
         * @details 기본 끔 — 켜기 전에 무거운 장면에서 Release 로 재어 이득을 확인한다. 켜면 드러나는 물체가 한 프레임 늦게 나온다(지난 프레임 깊이로 본다).
         */
        SW_GLOBAL_VARIABLE( bool, gv_occlusionCulling, kDefaultSettings._bOcclusionCulling != SW_FALSE, "GPU 컬링의 가림 판정 — 지난 프레임 장면 깊이의 타일 최대 깊이 뒤의 인스턴스를 거른다 (컬링이 있는 백엔드)" );
        /**
         * @brief `-gv_vertexPool=0` 이면 씬 메시 정점을 한 풀 버퍼에 모으지 않고 메시마다 자기 정점 버퍼로 그립니다.
         * @details 풀을 켠 이미지와 끈 이미지는 같아야 한다. 다르면 간접 인자의 startVertex 나 SV_VertexID 의 API 차이를 잘못 다룬 것이다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_vertexPool, kDefaultSettings._bVertexPool != SW_FALSE, "씬 메시 정점 풀 (false=메시마다 정점 버퍼, 진단용)" );
        /** @brief `-gv_shadowCache=1`: 서 있는 점광 · 스폿 그림자의 정적 메시를 캐시 층에 한 번 그려 다시 씁니다(그림은 같다). */
        SW_GLOBAL_VARIABLE( bool, gv_shadowCache, kDefaultSettings._bShadowCache != SW_FALSE, "서 있는 점광 · 스폿 그림자의 정적 메시를 캐시 층에 한 번 그려 다시 쓴다 (정적 메시 MeshComponent::_bStatic 만, 그림은 같다)" );
        /**
         * @brief `-gv_shadowCascadeCulling=1`: 주 시점의 방향광 그림자를 캐스케이드마다 따로 컬링합니다(캐스케이드마다 컬링 슬롯 하나) — 같은 바이너리로 전/후를 비교합니다.
         * @details 0(기본)이면 모든 타일을 덮는 합친 상자 하나로 거른다. 이득은 `perf-compare --cases shadowCascadeCulling` 이 정한다.
         *          그림은 같고 비용만 다른 성능 설정이라 배포본에도 둔다(언리얼 `r.Shadow.*` cvar 와 같은 자리).
         */
        SW_GLOBAL_VARIABLE( bool, gv_shadowCascadeCulling, kDefaultSettings._bShadowCascadeCulling != SW_FALSE, "방향광 그림자를 캐스케이드마다 따로 컬링한다(0 이면 모든 타일을 덮는 상자 하나 — 전/후 비교용)" );
        /**
         * @brief `-gv_psoAsync=0` 이면 머티리얼 변형 PSO 를 빌더 스레드가 아니라 렌더 스레드가 셋업 구간에서 모두 만듭니다(옛 동작 - A/B 측정 · 진단용).
         * @details 기본은 빌더 스레드입니다(`MaterialPSOBuilder`). 스레드를 쓸 수 없는 백엔드(GL)는 이 값과 상관없이 렌더 스레드가 프레임당 시간 예산 안에서 만듭니다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_psoAsync, kDefaultSettings._bPSOAsync != SW_FALSE, "머티리얼 변형 PSO 를 빌더 스레드가 만든다 (false=렌더 스레드가 동기로 만듦, A/B 측정용)" );
        /** @brief `-gv_upscalerSharpness=<단계>`: FSR 1 RCAS 의 선명도를 줄이는 단계입니다(0 = 가장 선명, 1 단계마다 절반 — AMD FSRRcasCon 의 sharpness). AMD 예제 기본값은 0.2 다. */
        SW_GLOBAL_VARIABLE( float32, gv_upscalerSharpness, kDefaultSettings._upscalerSharpness, "FSR 1 선명화(RCAS) 를 줄이는 단계 — 0 가장 선명, 1 마다 절반 (graphics.upscaler 가 spatial 일 때)" );
        /**
         * @brief `-gv_renderGraphParallelMinWidth=N`: 패스가 N 개보다 적은 레벨은 병렬로 기록하지 않고 렌더 스레드가 이어 기록합니다(기본 2 — 패스 하나짜리 레벨만 이어 기록).
         * @details 레벨마다 리스트를 열고 · 태스크로 넘겨 기다리고 · 제출하는 고정 비용이 좁은 레벨에서 이득을 지우는지 같은 실행에서 A/B 로 재는 스위치다(`RT.Graph.parallelLevel` · `RT.Graph.maxLevelWidth`).
         */
        SW_GLOBAL_VARIABLE( int32, gv_renderGraphParallelMinWidth, static_cast<int32>( kDefaultSettings._renderGraphParallelMinWidth ), "이보다 패스가 적은 렌더 그래프 레벨은 병렬로 기록하지 않는다 (2=패스 하나짜리 레벨만 이어 기록)" );
        /**
         * @brief `-gv_rhiImmediateSubmit=1`: 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 잘릴 때마다 바로 제출합니다.
         * @details 두 모드 모두 기록 순서가 곧 실행 순서다 — 즉시 모드는 제출 '시점'만 다르다. 제출 횟수가 늘어 오버헤드가 크지만 GPU 오류(DEVICE_HUNG, 검증 레이어)가 어느 제출에서 났는지 좁히기 쉽다.
         */
        SW_GLOBAL_VARIABLE( bool, gv_rhiImmediateSubmit, kDefaultSettings._bRHIImmediateSubmit != SW_FALSE, "RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼)" );

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

        struct RenderFrameSettingsInternal
        {
            /// 그림자 맵 한 변의 상한 — 밉 상한을 넘는 한 변은 첨부를 만들지 못한다.
            static constexpr int32 kMaxShadowMapResolution = static_cast<int32>( 1u << ( constant::render::kMaxTextureMipCount - 1 ) );
            /// 병렬 기록 레벨 너비의 하한 — 패스 하나짜리 레벨은 늘 이어 기록한다.
            static constexpr int32 kMinParallelWidth = 2;
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
        if ( engine::areEngineServicesBound() )
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
        settings._renderGraphParallelMinWidth     = static_cast<uint32>( MathUtil::max( static_cast<int32>( gv_renderGraphParallelMinWidth ), Internal::kMinParallelWidth ) );
        settings._meshMorphDebugMode              = gv_meshMorphDebugMode;
        settings._bGPUCulling                     = Internal::toFlag( gv_gpuCulling );
        settings._bDrawMerge                      = Internal::toFlag( gv_drawMerge );
        settings._bAliasTransients                = Internal::toFlag( gv_transientAliasing );
        settings._bAsyncCompute                   = Internal::toFlag( gv_asyncCompute );
        settings._bOcclusionCulling               = Internal::toFlag( gv_occlusionCulling );
        settings._bVertexPool                     = Internal::toFlag( gv_vertexPool );
        settings._bShadowCache                    = Internal::toFlag( gv_shadowCache );
        settings._bShadowCascadeCulling           = Internal::toFlag( gv_shadowCascadeCulling );
        settings._bPSOAsync                       = Internal::toFlag( gv_psoAsync );
        settings._bMotionBlur                     = Internal::toFlag( gv_motionBlur );
        settings._bRHIImmediateSubmit             = Internal::toFlag( gv_rhiImmediateSubmit );
        return settings;
    }
} // namespace sw
