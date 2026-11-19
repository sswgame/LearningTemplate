#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Config/RHIBackendType.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{

    /**
     * @struct WindowConfig
     * @brief 주 창과 기본 렌더링 백엔드 설정입니다. 창 제목은 팩 설정(`PackEngineSettings::_windowTitle`)이 정합니다.
     * @details 값은 `Config/Engine/EngineConfig.json` 에 있습니다(이 헤더는 형식만). 예외는 `_defaultRHI` 하나로, 출처가 쿠킹 표 `default_rhi_backend` 입니다.
     */
    REFLECT()
    struct SW_API WindowConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _clearColor{}; ///< 백버퍼 클리어 색(공백 또는 쉼표로 구분한 RGBA)

        PROPERTY( Min = 1 )
        uint32 _width{}; ///< 클라이언트 영역 너비(픽셀)

        PROPERTY( Min = 1 )
        uint32 _height{}; ///< 클라이언트 영역 높이(픽셀)

        PROPERTY()
        RHIBackend _defaultRHI{ RHIBackend::SW_RHI_BACKEND_DEFAULT }; ///< 명령줄이 고르지 않았을 때 쓸 백엔드(값의 출처는 쿠킹 표 `default_rhi_backend` — 파일에 적으면 그것이 이긴다)

        PROPERTY()
        bool _bVSync{}; ///< 수직 동기화 여부
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TextureFormatFamilySettings
     * @brief 압축 형식 묶음 하나입니다(BC · ASTC · ETC2). 용도마다 쓸 RHIFormat 이름을 적습니다 — 이 층은 RHI 를 모르므로 글이고 `TextureFormatFamily` 이 풀며, 모르는 이름은 오류입니다.
     * @details `_suffix` 는 쿠킹 변형의 파일 접미사입니다(`a.dds` → `a.astc.dds`). 비면 임포트한 DDS 자체가 이 묶음입니다. BC 의 색 형식은 임포트 규칙이 정해 여기 값은 장치 지원을 묻는 대표입니다.
     */
    REFLECT()
    struct SW_API TextureFormatFamilySettings
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        string _suffix{};
        PROPERTY()
        string _color{}; ///< 색 · 일반 데이터(임포트 규칙의 BC7 · BC3 · BC1)
        PROPERTY()
        string _normal{}; ///< 2채널 노멀맵(BC5)
        PROPERTY()
        string _mask{}; ///< 단일 채널(BC4)
        PROPERTY()
        string _hdr{}; ///< HDR(BC6H)
    };

    /** @struct TextureFormatTargetSettings @brief 배포 대상 하나의 형식 묶음 우선순위입니다. 실행 때 장치가 지원하는 첫 묶음을 고릅니다(웹은 두 벌 — 데스크톱 브라우저 BC, 모바일 브라우저 ASTC). */
    REFLECT()
    struct SW_API TextureFormatTargetSettings
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        vector<string> _listPlatform{}; ///< 이 대상을 쓰는 빌드 플랫폼(`build::kPlatformName` — Windows · Linux · Android · Web). 플랫폼 하나는 대상 하나에만 든다
        PROPERTY()
        vector<string> _listFamily{};
    };

    /** @struct TextureFormatSettings @brief 텍스처 형식 쿠킹 표입니다. Godot 의 플랫폼별 압축 형식 파일 · 언리얼 `TextureFormatPriority` 자리입니다(기억에 의존한 비교). */
    REFLECT()
    struct SW_API TextureFormatSettings
    {
        REFLECT_BODY();

        PROPERTY()
        vector<TextureFormatTargetSettings> _listTarget{}; ///< 실행은 빌드 플랫폼의 대상을 고르고(`findPlatformTarget`), 쿠킹은 모든 대상의 묶음을 굽는다(`resolveCookFamilies`)
        PROPERTY()
        vector<TextureFormatFamilySettings> _listFamily{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AudioConfig
     * @brief 오디오 서비스(`IAudioService::initialize`)가 읽는 설정입니다 — 출력 장치 다시 열기 간격과 클립 캐시 예산.
     * @details 다시 열기는 장치를 잃었거나 없이 시작했을 때 기본 장치를 다시 열어 보는 백오프다(지터를 섞어 두 배씩 상한까지). 헤드폰을 다시 꽂으면 곧 붙고(첫 간격),
     *          장치가 없는 기계는 상한마다 한 번만 본다. 언리얼 AudioMixer 의 장치 스왑 재시도 · 유니티 `OnAudioConfigurationChanged` 자리다.
     */
    REFLECT()
    struct SW_API AudioConfig
    {
        REFLECT_BODY();

        PROPERTY( Min = 1, Units = ms )
        uint32 _reopenFirstDelayMs{}; ///< 첫 다시 열기 간격의 위 끝(ms) — 첫 시도는 [0, 이 값] 안이다

        PROPERTY( Min = 1, Units = ms )
        uint32 _reopenMaxDelayMs{}; ///< 다시 열기 간격의 상한(ms) — `_reopenFirstDelayMs` 보다 작으면 오디오 초기화가 실패한다

        /**
         * @brief 디코드한 클립 캐시(`AudioClipCache`)의 예산(MB)입니다. 넘으면 재생 중이 아닌 클립을 가장 오래 안 쓴 것부터 뺀다.
         * @details 메모리 태그 예산(`MemoryBudget.json` 의 Audio)보다 작게 둔다 — 그 나머지가 스트리밍 디코더 · 믹서 몫이다. 언리얼 `Max Cache Size` · 유니티 `UnloadUnusedAssets` 자리다.
         */
        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _clipCacheMegabytes{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CharacterMeshCacheConfig
     * @brief 캐릭터 외형이 구운 · 합친 메시 캐시(`AppearanceBakedMeshCache`)의 설정입니다. 넘으면 그리는 쪽이 없는 차림을 가장 오래 안 쓴 것부터 뺀다.
     * @details `EngineConfig` 는 호스트 전용 서비스라 게임 모듈(GameFramework)이 못 본다 — 이 조각만 게임에 보이는 서비스로 따로 건다(`ConfigManager`, 자리는 `EngineConfig::_characterMeshCache`).
     *          메모리 태그 예산(`MemoryBudget.json` 의 Mesh)보다 작게 둔다. 캐시가 원본 메시도 쥐어 씬을 바꿔도 같은 차림을 다시 짓지 않는다 — 언리얼 Mutable 결과 캐시 · DDC 자리다.
     */
    REFLECT()
    struct SW_API CharacterMeshCacheConfig
    {
        REFLECT_BODY();

        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _budgetMegabytes{}; ///< 캐시 예산(MB). 0 이면(키가 없다) 캐시가 처음 남길 때 오류로 멈춘다

        SW_SERVICE_SLOT_CONST( CharacterMeshCacheConfig, Required );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct FileServeConfig
     * @brief 장치가 PC 의 팩 폴더를 받는 개발 전용 전송(`-fileserve=<주소>` — `FileServeContentSource` + 평문 HTTP 클라이언트 `PlainHTTPClient`)의 한도입니다.
     * @details 응답은 PC 쪽 서버(`py -3 -m Scripts fileserve`)가 보내지만 믿지 않는다 — 머리말 · 본문 상한과 요청 시한이 넘치면 그 요청은 실패다(언리얼 `HttpMaxContentLength` · 요청 시한 자리).
     *          본문 상한은 받는 구간(`FileServeContentSource::kChunkBytes`, 1 MiB) 이상이어야 한다 — 아니면 기동(`AndroidMain`)이 멈춘다. 배포 구성은 이 전송을 짓지 않는다.
     */
    REFLECT()
    struct SW_API FileServeConfig
    {
        REFLECT_BODY();

        PROPERTY( Min = 256 )
        uint32 _maxResponseHeaderBytes{}; ///< 상태 줄 + 헤더(빈 줄까지)의 바이트 상한 — chunked 의 구간 머리 · 트레일러에도 같은 상한
        PROPERTY( Min = 1 )
        uint32 _maxResponseBodyBytes{}; ///< 본문 바이트 상한(Content-Length · chunked 합 · 닫힘까지 모두)
        PROPERTY( Min = 1, Max = 600000, Units = ms )
        uint32 _requestTimeoutMs{}; ///< 잇기부터 응답 끝까지의 시한(요청 하나, 10 분까지 — 기다림 함수의 int32 ms 안)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TextureStreamingConfig
     * @brief 텍스처 밉 스트리밍의 정책 값입니다 — 렌더 스레드의 프레임당 제출 수, 위쪽 밉 풀 예산, 올리고 내리는 판단(`TextureStreamingPolicy`)의 값이 한 곳에 있다.
     * @details 언리얼 `r.Streaming.*`(PoolSize · MaxNumTexturesToStreamPerFrame) · 유니티 `QualitySettings.streamingMipmaps*` 자리다.
     */
    REFLECT()
    struct SW_API TextureStreamingConfig
    {
        REFLECT_BODY();

        /** @brief 렌더 스레드가 한 프레임에 새로 제출하는 비동기 텍스처 로드 수의 상한입니다(`TextureCache::completePendingLoads` — 한꺼번에 도착한 수백 장이 한 프레임을 늘리지 않게). */
        PROPERTY( Min = 1 )
        uint32 _uploadsPerFrame{};

        /** @brief 렌더 스레드가 한 프레임에 새로 제출하는 위쪽 밉 업로드 수의 상한입니다(`TextureCache::updateStreaming` — 텍스처 하나가 밉 사슬 전부라 첫 로드보다 무겁다). */
        PROPERTY( Min = 1 )
        uint32 _upgradesPerFrame{};

        /**
         * @brief 밉 스트리밍이 위쪽 밉으로 올린 텍스처 바이트 합의 상한(MB)입니다(`TextureStreamingPool` · `TextureStreamingPolicy` — 언리얼 `r.Streaming.PoolSize`).
         * @details 메모리 태그 예산(`MemoryBudget.json` 의 Texture — 모든 텍스처의 경고선)보다 작게 둔다. 그 나머지가 아래쪽 밉 · 렌더 텍스처 · CPU 텍스처 몫이다.
         */
        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _poolMegabytes{};

        /**
         * @brief 위쪽 밉을 범위로 읽는 중이거나 읽어 제출을 기다리는 바이트 합의 상한(MB)입니다(`TextureCache::updateStreaming`, MR-2 — 대기 줄은 바이트를 쥐지 않는다).
         * @details 유니티 `QualitySettings.streamingMipmapsMaxFileIORequests` 자리를 바이트로 잰다(텍스처마다 크기가 달라 수보다 바이트가 RAM 을 묶는다). 하나가 이보다 크면 혼자일 때만 읽는다.
         */
        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _maxMegabytesInFlight{};

        /** @brief 화면 크기를 알리지 않은 지 이만큼 지난 텍스처는 필요 없는 것으로 본다 — 예산을 넘으면 가장 먼저 내리고, 다시 보일 때까지 올리지 않는다(`TextureStreamingPolicy`). */
        PROPERTY( Min = 1 )
        uint32 _idleFramesBeforeRelease{};

        /**
         * @brief 텍스처 하나의 밉을 바꾼 뒤 다시 바꾸기까지의 최소 프레임 수입니다(`TextureStreamingPolicy` — 경계에서 올렸다 내렸다를 되풀이하지 않게, R6-6).
         * @details 언리얼 텍스처 스트리머가 밉을 바꾼 텍스처를 잠시 다시 고르지 않는 자리다. 바이트는 이 동안에도 예산에 센다.
         */
        PROPERTY( Min = 1 )
        uint32 _minFramesBetweenChanges{};

        /** @brief 올리기는 예산에서 이 비율(%)만큼 여유를 남긴 선까지만 한다 — 내리기는 예산을 넘을 때라 그 사이가 흔들림을 막는 띠다(`TextureStreamingPolicy`, R6-6). */
        PROPERTY( Min = 0, Max = 50, Meta = "Units=%" )
        uint32 _promoteBudgetMarginPercent{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SkyLightConfig
     * @brief 스카이 라이트(하늘을 거칠기 단계마다 걸러 프로브 밖 환경광으로 쓰는 것 — `SkyLightCache`)의 정책 값입니다.
     * @details 언리얼 Sky Light 의 Cubemap Resolution · Real Time Capture, 유니티 Environment Reflections 의 Resolution, Godot Sky 의 Radiance Size · Process Mode 자리다.
     *          거르기는 CPU 가 하늘이 바뀐 프레임에만 한다 — 비용은 한 변² × (단계 수 − 1) × 표본 수에 비례한다.
     */
    REFLECT()
    struct SW_API SkyLightConfig
    {
        REFLECT_BODY();

        /** @brief 옥타헤드럴 한 변 텍셀 수입니다(`ReflectionProbeAsset::kMinResolution` ~ `kMaxResolution`). 단계마다 같은 크기다. */
        PROPERTY( Min = 4, Max = 256 )
        uint32 _resolution{};

        /** @brief 거칠기 단계 수입니다 — 단계 l 의 거칠기는 l / (단계 수 − 1) 이다(`ReflectionProbeAsset::kMinLevelCount` ~ `kMaxLevelCount`). */
        PROPERTY( Min = 2, Max = 8 )
        uint32 _levelCount{};

        /** @brief 거친 단계의 텍셀 하나를 만드는 GGX 중요도 표본 수입니다(`ReflectionProbeAsset::filterRoughnessLevels`). */
        PROPERTY( Min = 1 )
        uint32 _filterSampleCount{};

        /**
         * @brief 거르기 원본의 밉 수 상한입니다(밉 0 포함 — `OctahedralRadianceMap::buildMips`). 표본마다 pdf 로 이 사슬의 밉을 골라 적은 표본으로 잡음 없이 거른다(filtered importance sampling).
         * @details 1 이면 밉 0 만 읽는 보통 중요도 표본이다. 원본 한 변이 홀수 · 1 이 되면 거기서 멈춘다 — 언리얼 · 유니티는 큐브맵 밉 전부를 쓴다.
         */
        PROPERTY( Min = 1, Max = 11 )
        uint32 _filterMipCount{};

        /**
         * @brief 같은 하늘의 값이 바뀔 때(시간대 · 날씨) 다시 거르기까지의 최소 프레임 수입니다. 처음 보는 하늘은 기다리지 않고 그 프레임에 거른다.
         * @details 언리얼 Real Time Capture 가 캡처를 여러 프레임에 나눠 주기를 두는 자리다 — 해가 프레임마다 조금씩 움직여도 이 간격에 한 번만 거른다.
         */
        PROPERTY( Min = 1 )
        uint32 _captureIntervalFrames{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ProbeVisibilityConfig
     * @brief 프로브 GI 의 빛샘 막기 계수입니다 — 소비 셰이더(lighting.hlsli `LightData::sampleProbeVolume`)가 이웃 프로브 여덟의 가중에 쓴다. 쿠킹 define `SW_PROBE_*` 로 간다.
     * @details DDGI(RTXGI · Adria) 모양이다: 표면 위치를 노멀 · 시선 쪽으로 띄우고(자기 그림자 바이어스), 표면 뒤에 있는 프로브를 코사인으로 줄이고(뒷면 가중),
     *          실시간 볼륨은 프로브마다의 거리 모먼트로 체비쇼프 가시성을 곱한다(벽 너머 프로브가 0 에 가깝다). 언리얼 Lumen 은 스크린 프로브 · 표면 캐시라 이 자리가 없고,
     *          유니티 APV 의 Virtual Offset · 누수 감소(Leak Reduction) · Godot SDFGI 의 프로브 가림이 같은 자리다. C++ 가중 식은 `ProbeVisibility` 다(테스트가 본다).
     */
    REFLECT()
    struct SW_API ProbeVisibilityConfig
    {
        REFLECT_BODY();

        /** @brief 표면을 노멀 쪽으로 띄우는 거리 — 볼륨의 가장 짧은 프로브 간격에 대한 비율입니다(`SW_PROBE_NORMAL_BIAS`). */
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _normalBias{};

        /** @brief 표면을 카메라 쪽으로 띄우는 거리 — 볼륨의 가장 짧은 프로브 간격에 대한 비율입니다(`SW_PROBE_VIEW_BIAS`). */
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _viewBias{};

        /** @brief 표면 뒤 프로브의 가중 하한(0 ~ 1)입니다 — 프로브 쪽이 노멀과 반대면 이 값, 노멀 쪽이면 1 이다(`SW_PROBE_BACKFACE_FLOOR`). 0 이면 뒤 프로브만 있는 곳이 검어진다. */
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _backfaceFloor{};

        /** @brief 체비쇼프 가시성의 지수입니다 — 클수록 벽 너머 프로브를 세게 자른다(DDGI 의 세제곱이 3, `SW_PROBE_CHEBYSHEV_EXPONENT`). */
        PROPERTY( Min = 1.0, Max = 8.0 )
        float32 _chebyshevExponent{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderConfig
     * @brief 렌더러 정책 값입니다(감사 RN-16 — C++ 상수에 흩어졌던 렌더 정책을 이 한 자리로 옮긴다. 언리얼 `r.*` 콘솔 변수의 바탕 층 · 유니티 SRP 에셋 자리).
     * @details 셰이더가 읽는 값은 쿠킹 define 한 경로로만 간다(`ShaderCompiler::appendEngineDefines` — 도구 지문에 섞여 값이 바뀌면 셰이더 캐시가 함께 낡는다).
     *          C++ 가 읽는 값은 부르는 쪽이 이 구조체를 인자로 넘긴다. 셰이더 버퍼 배치를 정하는 상수(라이트 그리드 16x9x24 · 프레임 라이트 상한 1024)는
     *          binding_slots.hlsli 한 곳이 C++ · HLSL 의 공통 원천이고 groupshared 크기 · 스레드 수를 컴파일 때 검사하므로 여기 두지 않는다.
     */
    REFLECT()
    struct SW_API RenderConfig
    {
        REFLECT_BODY();

        /**
         * @brief 빌더 스레드를 쓰지 않는 백엔드(GL)에서 렌더 스레드가 한 프레임에 머티리얼 PSO 를 만드는 시간 예산(µs)입니다(`FramePSOManager::pollMaterialPSOBuilds`).
         * @details 줄이 있으면 예산과 상관없이 최소 하나는 만든다. 언리얼 `r.ShaderPipelineCache.BatchTime` 자리다.
         */
        PROPERTY( Min = 1, Meta = "Units=us" )
        uint32 _psoBuildBudgetMicroseconds{};

        /** @brief 방향광 캐스케이드 분할에서 로그 분할의 몫(0 = 균등, 1 = 로그) — GPU Gems 3 실용 분할(언리얼 CascadeDistributionExponent · 유니티 분할 비율 자리). */
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _shadowCascadeSplitLambda{};

        /** @brief 점광 · 스폿 그림자의 깊이 바이어스(텍셀 수) — 원근 칸은 텍셀 크기가 거리에 비례하므로 셰이더가 거리를 곱한다(`FrameShadowData::populateLocalShadows`). */
        PROPERTY( Min = 0.0 )
        float32 _shadowLocalDepthBiasTexels{};

        /**
         * @brief 점광 · 스폿 그림자 칸 한 변의 하한(텍셀)입니다(`LocalShadowAtlas::build` — 화면에서 작은 빛 · 칸이 넘치는 프레임에 칸을 반씩 줄이되 이 아래로는 줄이지 않는다, 감사 RN-4).
         * @details 유니티 URP 추가 광 그림자 해상도 단계의 가장 낮은 칸 자리다. 이 아래로 줄이면 칸 가장자리 여유(PCF 3 텍셀)가 칸을 다 먹는다.
         */
        PROPERTY( Min = 16 )
        uint32 _shadowLocalMinTileResolution{};

        /**
         * @brief 그림자를 드리우는 점광 · 스폿마다 따로 둘 그림자 컬링 슬롯의 상한입니다(감사 RN-5). 넘는 빛의 타일은 합친 그림자 칸을 쓴다.
         * @details 슬롯 하나가 인스턴스 수만큼의 가시 목록 · 간접 인자 버퍼를 가지므로 메모리와 디스패치 수의 상한이다. 언리얼 · 유니티는 그림자 절두체마다 컬링한다.
         */
        PROPERTY( Min = 0 )
        uint32 _shadowLocalCullSlotLimit{};

        /** @brief TAA 기록 비율 — 화면이 그대로인 곳의 누적 정도(taa.hlsl `SW_TAA_HISTORY_WEIGHT` 쿠킹 define). 1 이면 이번 그림이 들지 않으므로 1 미만이어야 한다. */
        PROPERTY( Min = 0.0, Max = 0.99 )
        float32 _taaHistoryWeight{};

        /**
         * @brief TAA 기록 비율의 하한 — 빠르게 움직이거나 기록을 믿기 어려운 픽셀의 누적 정도(taa.hlsl `SW_TAA_MIN_HISTORY_WEIGHT`). `_taaHistoryWeight` 이하여야 한다.
         * @details 블렌드는 기록 신뢰도(클리핑 전후 휘도 차) · 화면 속도로 이 값과 `_taaHistoryWeight` 사이를 고른다(`TemporalMath::computeHistoryWeight` — temporal_math.hlsli, Adria · Playdead TAA 의 feedback min · max).
         */
        PROPERTY( Min = 0.0, Max = 0.99 )
        float32 _taaMinHistoryWeight{};

        /// @brief 시간 업스케일 렌더 배율의 하한입니다(언리얼 `r.ScreenPercentage` 의 TSR 권장 하한 50 % 아래까지 둔다 — 그 아래는 표본이 모자라 흐리다).
        static constexpr float32 kMinTemporalUpscaleRenderScale = 0.25f;
        /**
         * @brief 시간 업스케일(TAAU)의 렌더 해상도 배율 — 장면을 출력 해상도 × 이 값으로 그리고 TAA 가 출력 해상도로 키운다(언리얼 1 차 화면 배율 `r.ScreenPercentage` 와 TAAU).
         * @details 1 이면 두 해상도 공간이 같아 업스케일 없이 지금과 같은 경로다. 출력 공간 첨부(`RenderPassAttachment::_resolutionSpace`)를 선언한 파이프라인(디퍼드)에만 든다.
         *          값이 없거나 범위 밖이면 멈춘다(`FrameViewResources::getTemporalUpscaleRenderScale`). 품질 단계(RenderQuality.json)가 낮을수록 작게 둔다(언리얼 확장성 단계와 같은 자리).
         */
        PROPERTY( Min = 0.25, Max = 1.0 )
        float32 _temporalUpscaleRenderScale{};

        /** @brief TAA 분산 클리핑 폭 γ — YCoCg 이웃 평균 ± γσ 상자로 기록을 자른다(taa.hlsl `SW_TAA_VARIANCE_CLIP_GAMMA`, Salvi 2016 은 1 · 언리얼은 1 근처). 작을수록 고스팅이 줄고 깜빡임이 는다. */
        PROPERTY( Min = 0.1, Max = 4.0 )
        float32 _taaVarianceClipGamma{};

        /** @brief 이 화면 속도(px/프레임) 이상이면 TAA 기록 비율이 하한까지 내려간다(taa.hlsl `SW_TAA_VELOCITY_REJECT_PIXELS`). 0 보다 커야 한다. */
        PROPERTY( Min = 0.01 )
        float32 _taaVelocityRejectPixels{};

        /**
         * @brief 자동 노출 히스토그램에서 버리는 어두운 쪽 몫(%, auto_exposure.hlsl `SW_AUTO_EXPOSURE_LOW_PERCENT`). 남은 구간의 평균 로그 휘도로 노출을 정한다.
         * @details 언리얼 Auto Exposure Histogram 의 Low Percent · High Percent 자리다 — 화면 대부분이 하늘이거나 검은 띠가 있는 장면에서 평균이 끝으로 끌리지 않는다.
         */
        PROPERTY( Min = 0.0, Max = 100.0 )
        float32 _autoExposureLowPercent{};

        /** @brief 자동 노출 히스토그램에서 남기는 위 끝(%, `SW_AUTO_EXPOSURE_HIGH_PERCENT`) — 이보다 밝은 몫(하이라이트 · 광원)은 버린다. `_autoExposureLowPercent` 보다 커야 한다. */
        PROPERTY( Min = 0.0, Max = 100.0 )
        float32 _autoExposureHighPercent{};

        /** @brief 자동 노출 히스토그램이 덮는 log2 휘도의 아래 끝(`SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN`, 언리얼 Histogram Log Min). 이보다 어두운 텍셀은 첫 칸에 든다. */
        PROPERTY()
        float32 _autoExposureHistogramLogMin{};

        /** @brief 자동 노출 히스토그램이 덮는 log2 휘도의 위 끝(`SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX`). 아래 끝보다 커야 한다. 이보다 밝은 텍셀은 끝 칸에 든다. */
        PROPERTY()
        float32 _autoExposureHistogramLogMax{};

        /**
         * @brief 한 프레임에 자리를 받는 추가 뷰(캡처 카메라 · 화면 사각형 · 에디터 씬 뷰)의 수입니다. 넘치면 스케줄러가 보이는 뷰 · 늦은 뷰 먼저 준다(`RenderViewScheduler`).
         * @details 뷰마다 컬링 슬롯 둘 · 상수 버퍼 · 트랜지언트 풀이 하나씩 든다. 상한은 `kExtraRenderViewCapacity`(RenderView.h)와 같다 — 범위 밖이면 첫 프레임이 멈춘다(`RenderViewCollector::getMaxExtraViewCount`).
         */
        PROPERTY( Min = 1, Max = 32 )
        uint32 _maxExtraViewCount{};

        /** @brief 텍스처 밉 스트리밍 설정입니다(프레임당 제출 수 · 풀 예산 · 내리기까지의 유휴 프레임 — `TextureStreamingConfig`). 스트리밍 값은 이 칸 한 곳이다. */
        PROPERTY()
        TextureStreamingConfig _textureStreaming{};

        /** @brief 스카이 라이트 설정입니다(거르기 크기 · 단계 수 · 표본 수 · 다시 거르는 간격 — `SkyLightConfig`). */
        PROPERTY()
        SkyLightConfig _skyLight{};

        /** @brief 프로브 GI 의 빛샘 막기 계수입니다(바이어스 · 뒷면 가중 · 체비쇼프 지수 — `ProbeVisibilityConfig`). */
        PROPERTY()
        ProbeVisibilityConfig _probeVisibility{};

        /**
         * @brief 반사 프로브 베이크가 거르는 원본(단계 0)의 밉 수 상한입니다(`ReflectionProbeAsset::makeFromFaces`, `SkyLightConfig::_filterMipCount` 와 같은 뜻).
         * @details 표본 수는 프로브마다의 칸(`ReflectionProbeComponent` Samples Per Texel)이다. 베이크는 에디터에서 한 번이라 스카이 라이트와 따로 둔다(언리얼도 캡처 설정이 따로다).
         */
        PROPERTY( Min = 1, Max = 11 )
        uint32 _reflectionProbeFilterMipCount{};
    };
} // namespace sw

namespace sw
{
    /**
     * @enum RHIDiagnosticSwitch
     * @brief 장치를 만들 때 켜는 GPU 진단(검증 층 · DRED · GPU 브레드크럼)의 기본값입니다. 진단마다 시작 인자(`-rhi-validation` · `-rhi-dred` · `-rhi-breadcrumbs`, `=0` 은 끔)가 이긴다(`resolveRHIDiagnosticSwitch`).
     * @details 진단은 장치를 만들기 전에만 걸 수 있어 시작할 때 한 번 정한다 — 실행 중에 바꾸는 개발 변수가 아니다. 언리얼 Development 빌드의 `-d3ddebug` · `-gpucrashdebugging` 과 같다.
     *          게임은 Release 에서만 지어지므로 게임 결함은 Release 에서 인자로 켜서 본다.
     */
    ENUM()
    enum class RHIDiagnosticSwitch : uint8
    {
        None            = 0, ///< 파일에 값이 없다 — 장치 생성이 오류로 멈춘다(짐작하지 않는다)
        Off             = 1, ///< 끈다
        On              = 2, ///< 모든 구성에서 켠다(그 진단이 없는 구성은 듣지 않는다 — 검증 층은 배포 구성에 없다)
        DebugBuildOnly  = 3, ///< Debug 구성만 켠다
        NonShippingOnly = 4, ///< 배포가 아닌 구성(Debug · Release)만 켠다
    };

    /**
     * @enum RHIBackBufferFormat
     * @brief 요청할 수 있는 백버퍼 포맷입니다(EngineConfig `_rhi._backBufferFormat`). 언리얼 `r.DefaultBackBufferPixelFormat` 과 같은 자리입니다.
     * @details `RHIFormat` 전체가 아니라 백버퍼로 쓸 수 있는 둘만 담는다 — 깊이 · 압축 포맷을 요청하는 모양을 만들 수 없게 한다.
     *          백엔드가 실제로 채택한 값은 `IRHIDevice::getBackBufferFormat()` 이 답한다. 장치를 만들 때 한 번 읽는 기동 값이다.
     */
    ENUM()
    enum class RHIBackBufferFormat : uint8
    {
        RGBA8, ///< R8G8B8A8_UNORM — 계약 기본값(`constant::render::kBackBufferFormat`)
        BGRA8, ///< B8G8R8A8_UNORM — 백버퍼를 타깃으로 하는 PSO 가 이 포맷으로도 만들어지는지 돌려 보는 스위치
    };

    /**
     * @struct RHIConfig
     * @brief 렌더 장치(`IRHIDevice::initialize`)가 읽는 정책 값입니다. 장치는 `RHISwapChainDesc` 로 받는다.
     * @details 값은 모두 `Config/Engine/EngineConfig.json` 의 `_rhi` 에 있다 — 빠진 숫자 키는 범위 검사(`Min`)에서 로드 오류다.
     *          검증 스위치(검증 층 · GBV · syncval)는 Engine 의 장치가 읽어야 해서 DevTools 만 읽는 `Config/Dev` 가 아니라 여기 둔다. 배포가 아닌 구성에서만 뜻이 있다.
     */
    REFLECT()
    struct SW_API RHIConfig
    {
        REFLECT_BODY();

        /**
         * @brief 장치 메모리 할당 수 한도(Vulkan `maxMemoryAllocationCount`)에서 남겨 둘 칸입니다. 살아 있는 할당이 `한도 - 이 값` 에 닿으면 새 할당을 오류로 거절한다(R-4).
         * @details 남긴 칸은 드라이버 · 레이어 · 오버레이가 같은 장치에서 쓰는 몫이다. 언리얼 Vulkan 의 `r.Vulkan.MaxAllocations` 경고 자리다.
         */
        PROPERTY( Min = 1 )
        uint32 _allocationCountReserve{};

        /**
         * @brief 다른 스레드 · 프레임 밖 버퍼 업로드가 쓰는 스테이징 블록 하나의 크기(MB)입니다(Vulkan 업로드 커맨드버퍼 — 블록을 나눠 쓰고 프레임 펜스 뒤에 돌려받는다).
         * @details 한 업로드가 블록보다 크면 그 업로드만 전용 스테이징을 쓴다. DX12 의 구조 버퍼 스테이징 슬롯 자리다.
         */
        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _uploadStagingMegabytes{};

        /** @brief 업로드 스테이징 블록이 이만큼 프레임 동안 쓰이지 않으면 놓습니다(블록 하나는 늘 남긴다). 업로드가 몰린 뒤의 상주 메모리를 돌려준다. */
        PROPERTY( Min = 1 )
        uint32 _uploadStagingIdleFrames{};

        /** @brief 파이프라인 캐시(Vulkan `VkPipelineCache`)를 디스크에 저장하는 주기(초)입니다. 종료 전에 죽어도 그 주기 앞까지의 컴파일이 남는다(R-13). */
        PROPERTY( Min = 1, Units = s )
        float64 _pipelineCacheSaveIntervalSeconds{};

        /**
         * @brief 요청 백버퍼 포맷입니다(`RHIBackBufferFormat`, 언리얼 `r.DefaultBackBufferPixelFormat`). 백엔드가 실제로 채택한 값은 `IRHIDevice::getBackBufferFormat()` 이다.
         * @details DX 는 요청대로, Vulkan 은 서피스와 협상, GL 은 창 픽셀 포맷이라 늘 기본이다. 백버퍼를 타깃으로 하는 PSO 는 채택값으로 만든다.
         */
        PROPERTY( Required )
        RHIBackBufferFormat _backBufferFormat{};

        /**
         * @brief 소프트웨어 어댑터(WARP · CPU Vulkan)로 띄웁니다 — GPU 없는 CI 러너와 같은 래스터라이저를 이 PC 에서 고른다. 환경 변수 `SW_RHI_SOFTWARE_ADAPTER=1` 도 켠다.
         * @details 배포본에도 듣는다 — GPU 없는 기계에서 띄우는 길이다(언리얼 `-warp` 도 모든 구성에서 받는다). 언제 켤지는 엔진이, 어떻게 고를지는 백엔드가 정한다.
         */
        PROPERTY( Required )
        bool _bSoftwareAdapter{};

        /**
         * @brief 하드웨어 어댑터를 저전력 순서(하이브리드 노트북의 내장 GPU)로 고릅니다. 끄면 고성능 순서(OS 의 GPU 선호 · Vulkan 장치 종류)다 — 언리얼 `-preferIntel` 자리.
         * @details DX11 · DX12 · Vulkan 만 듣는다. GL 은 어댑터를 고르는 API 가 없어 실행 파일 내보내기(NvOptimusEnablement · AmdPowerXpress…)를 따른다.
         */
        PROPERTY( Required )
        bool _bPreferMinimumPower{};

        /**
         * @brief 업로드 스테이징 용량(MB)입니다. GL · Vulkan 은 링 크기(이보다 큰 업로드 하나는 링이 빌 때 혼자 들어간다), DX11 은 복사 중인 스테이징 텍스처 합의 상한이다.
         * @details 64 는 측정값이 아니라 4096² BC7 밉 체인(약 21 MiB) 몇 장을 동시에 담는 추정이다(RHIAsyncUploads 2.2, M1 이 조정한다).
         */
        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _stagingRingMegabytes{};

        /**
         * @brief 텍스처 되읽기 링 용량(MB)입니다(GL · Vulkan). 이보다 큰 읽기 하나는 링이 빌 때 혼자 들어간다.
         * @details 64 는 측정값이 아니라 4K RGBA16F 렌더 타깃 한 장(약 63.3 MiB)을 담는 추정이다.
         */
        PROPERTY( Min = 1, Meta = "Units=MB" )
        uint32 _readbackRingMegabytes{};

        /**
         * @brief 링보다 큰 업로드 · 텍스처 읽기 한 장을 일회성 버퍼로 받는 상한(MB)입니다(GL · Vulkan). 넘으면 `Rejected`, 0 이면 링보다 큰 한 장은 모두 `Rejected` 다.
         * @details 512 는 측정값이 아니라 8192² RGBA8 그림자 스냅샷(256 MiB)과 8K RGBA16F 뷰(약 253 MiB)를 받고 그 두 배에서 멈추는 추정이다. 한도가 없으면 잘못된 요청 하나가 그 크기의 버퍼를 만든다.
         */
        PROPERTY( Required, Meta = "Units=MB" )
        uint32 _stagingSingleMaxMegabytes{};

        /**
         * @brief 이 크기(KB) 이하의 텍스처 업로드는 스테이징 없이 바로 올립니다(지금은 DX11 `UpdateSubresource` 만). 0 이면 모두 스테이징이다.
         * @details 256 은 추정이다 — 잠금 안의 복사가 수십 µs 라 프레임에 비해 작고, 스테이징 한 장의 생성 · 추가 복사의 고정 비용과 비슷한 자리다(M1 이 조정한다).
         */
        PROPERTY( Required, Meta = "Units=KB" )
        uint32 _directUploadMaxKilobytes{};

        /**
         * @brief 그래픽 API 검증 층(D3D12 · D3D11 디버그 레이어, Vulkan 검증 레이어)을 켤지의 기본값입니다. 시작 인자 `-rhi-validation` 이 이긴다.
         * @details 검증 층은 배포가 아닌 구성(`SW_DO_CHECKS`)에만 컴파일돼 있다. 배포 구성은 이 값을 듣지 않는다.
         */
        PROPERTY()
        RHIDiagnosticSwitch _validationLayer{ RHIDiagnosticSwitch::None };

        /**
         * @brief D3D12 DRED(Device Removed Extended Data — 자동 브레드크럼 · 페이지 폴트)를 켤지의 기본값입니다. 시작 인자 `-rhi-dred` 가 이긴다.
         * @details DRED 는 런타임 기능이라 배포 구성에서도 인자로 켤 수 있다(플레이어 기계의 장치 소실을 좇을 때, 언리얼 `-gpucrashdebugging`). 패스 표지(`_gpuBreadcrumbs`)와는 따로다.
         */
        PROPERTY()
        RHIDiagnosticSwitch _dred{ RHIDiagnosticSwitch::None };

        /**
         * @brief GPU 브레드크럼(패스마다 GPU 표지 · Vulkan 체크포인트 — 장치 소실 뒤 마지막 패스 이름)을 켤지의 기본값입니다. 시작 인자 `-rhi-breadcrumbs` 가 이긴다.
         * @details 표지 한 번이 패스당 GPU 쓰기 하나라 값싸다. 배포 구성에서도 인자로 켤 수 있다(언리얼 `-gpucrashdebugging`). DRED(`_dred`)와는 따로다.
         */
        PROPERTY()
        RHIDiagnosticSwitch _gpuBreadcrumbs{ RHIDiagnosticSwitch::None };

        /** @brief GPU 기반 검증(D3D12 `SetEnableGPUBasedValidation` · Vulkan `VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT`)을 켭니다. 디버그 레이어가 켜진 실행에서만 듣는다(R-9). */
        PROPERTY()
        bool _bGPUBasedValidation{};

        /** @brief Vulkan 동기화 검증(`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`)을 켭니다. 검증 레이어가 켜진 실행에서만 듣는다(R-9). */
        PROPERTY()
        bool _bSynchronizationValidation{};
    };

    /**
     * @enum RHIDeviceLostPolicy
     * @brief 렌더 장치를 잃었을 때(TDR · 드라이버 재시작 · 개발 명령 `rhi.removeDevice`) 엔진이 할 일입니다. `EngineRenderDriver` 가 프레임마다 `IRHIDevice::isDeviceLost` 를 보고 고른다.
     * @details 언리얼은 `GetDeviceRemovedReason` → DRED 덤프 → 크래시 리포트 뒤 종료하고 Godot 4 도 치명 오류로 끝낸다(기본값 `Quit`).
     *          유니티는 D3D11 장치를 잃으면 장치를 다시 만들고 자원을 다시 올린다 — 그 자리가 `Recreate` 다. 다시 만드는 길은 백엔드 교체와 같은 하나
     *          (모듈 내리기 → `RHI::recreateDevice` → 자원 다시 올리기 → 모듈 세우기, App 의 `EditorModuleManager::applyPendingBackendChange` — 개발 · 배포 공통)이고,
     *          다시 만들기가 어려운 백엔드(`RHI::canRecreateAfterDeviceLost` — OpenGL 은 컨텍스트 · 창 픽셀 포맷, WebGPU 는 브라우저 장치)는 `Quit` 로 답한다.
     */
    ENUM()
    enum class RHIDeviceLostPolicy : uint8
    {
        None     = 0, ///< 파일에 값이 없다 — 기동이 오류로 멈춘다(정책을 짐작하지 않는다)
        Quit     = 1, ///< 손실을 처음 본 백엔드가 브레드크럼을 보고한 뒤, 그 프레임에 `ProcessExitCode::DeviceLost` 로 끝낸다
        Recreate = 2, ///< 같은 백엔드로 장치를 다시 만든다(프레임 끝의 재생성 길). 백엔드가 다시 만들 수 없으면 `Quit` 와 같고, 다시 만들기가 실패해도 `DeviceLost` 로 끝낸다
    };

    /**
     * @struct EngineConfig
     * @brief `Config/Engine/EngineConfig.json` 이 담는 엔진 기동 설정입니다.
     * @details 이 헤더는 형식(타입 · 범위)만 정하고 값은 모두 그 파일에 있습니다(언리얼 `BaseEngine.ini` 와 같은 바탕 층). 그래서 파일은 모든 키를 적고
     *          (`ConfigFileSchemaTest` 가 빠진 키를 막는다), 빠진 숫자 키는 범위 검사(`Min`)에서 로드 오류가 됩니다. 기동은 파일을 읽은 뒤에만 이 값을 봅니다
     *          (소유자는 `ConfigManager` 이고 읽는 쪽은 `engine::getSubsystem<EngineConfig>()` 다 - 정적 활성본이 없어 표마다 값이 따로 있고, 다시 읽기는 제자리다).
     *          파일을 읽기 전에 필요한 리소스 우선순위는 구성 때 베이크한 같은 파일의 사본(`AssetPaths::getDefaultSearchPriority`)에서 읽습니다.
     *          `ConfigBaseLayer` 메타는 프로젝트 설정 창이 이 파일을 "다른 값만" 이 아니라 모든 키로 쓰게 합니다(`ProjectSettingsDocument`).
     */
    REFLECT( Meta = "ConfigBaseLayer" )
    struct SW_API EngineConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        WindowConfig _window; ///< 창·백엔드 설정

        /** @brief 한 프레임이 인정하는 최대 가변 델타(초)입니다. 디버거 정지 같은 긴 멈춤을 잘라 냅니다. */
        PROPERTY( Min = 0.001, Units = s )
        float32 _maxFrameDeltaTime{};

        /**
         * @brief 고정 주기 한 스텝의 길이(초)입니다. 기본은 60Hz 입니다.
         * @details 고정 스텝 값의 출처는 이것 하나다 — 게임 `fixedUpdate` 와 씬 물리가 같이 쓴다(물리는 `PhysicsSettings::_subStepCount` 로 이 스텝을 나눈다,
         *          유니티 `Time.fixedDeltaTime` 과 같다).
         */
        PROPERTY( Min = 0.001, Units = s )
        float32 _fixedDeltaTime{};

        /**
         * @brief 한 프레임이 돌릴 수 있는 고정 스텝 수의 상한입니다.
         * @details 상한을 넘긴 남은 시간은 버립니다. 남기면 느린 프레임이 더 많은 스텝을 불러
         *          더 느려지는 악순환이 됩니다. FixedTimestep 이 이 값을 적용합니다.
         */
        PROPERTY( Min = 1 )
        uint32 _maxFixedStepPerFrame{};

        PROPERTY()
        vector<string> _listResourcePriority{}; ///< 리소스 팩 탐색 우선순위(앞이 먼저)

        /** @brief 데이터 상속(`_base`, `DataInheritance`) 사슬의 깊이 상한입니다. 넘으면 그 파일은 로드 오류입니다. */
        PROPERTY( Min = 1 )
        uint32 _dataInheritanceMaxDepth{};

        /**
         * @brief 패치 목록 서명 확인용 공개 키(ES256 P-256)의 x 좌표입니다(64 자리 16 진). `_patchTrustPublicKeyYHex` 와 함께 비어 있으면 서명을 요구하지 않습니다(스토어가 무결성을 맡는 채널).
         * @details 공개 키는 비밀이 아니다. 개인 키는 이 파일에 두지 않고 패치를 만드는 쪽이 `-patch-sign-key <PEM 경로>` 로만 받는다(`PackPatchTrustSource`).
         */
        PROPERTY()
        string _patchTrustPublicKeyXHex{};

        /** @brief 패치 목록 서명 확인용 공개 키의 y 좌표입니다(64 자리 16 진). */
        PROPERTY()
        string _patchTrustPublicKeyYHex{};

        PROPERTY()
        TextureFormatSettings _textureFormats{}; ///< 대상별 텍스처 압축 형식 목록

        PROPERTY()
        AudioConfig _audio{}; ///< 오디오 출력 장치 다시 열기 간격 · 클립 캐시 예산

        PROPERTY()
        CharacterMeshCacheConfig _characterMeshCache{}; ///< 캐릭터 외형이 구운 · 합친 메시 캐시의 예산(게임 모듈이 보는 조각)

        PROPERTY()
        FileServeConfig _fileServe{}; ///< 개발 전용 장치 전송(Fileserve)의 응답 한도 · 시한

        PROPERTY()
        RenderConfig _render{}; ///< 렌더러 정책 값(PSO 예산 · …)

        /**
         * @brief 창이 배경(포커스 없음)일 때도 패드 입력을 게임에 넣을지입니다(감사 P-10).
         * @details XInput · evdev 는 창과 상관없이 읽혀, 끄지 않으면 다른 창을 쓰는 동안 패드가 게임을 움직인다. SDL `JOYSTICK_ALLOW_BACKGROUND_EVENTS`(기본 끔)와 같은 자리다.
         */
        PROPERTY()
        bool _bBackgroundGamepadInput{};

        PROPERTY()
        RHIDeviceLostPolicy _deviceLostPolicy{ RHIDeviceLostPolicy::None }; ///< 렌더 장치를 잃었을 때 할 일(`None` 이면 기동이 오류로 멈춘다)

        PROPERTY()
        RHIConfig _rhi{}; ///< 렌더 장치 정책 값(할당 수 여유 · 파이프라인 캐시 저장 주기 · 검증 스위치)

        /**
         * @brief 셰이더로 보내는 시간(머티리얼 시간 · VAT 시계 · 시간 구동 컴퓨트)을 접는 주기(초)입니다(`TimeRolloverUtil::fold`).
         * @details 셰이더 칸이 float32 라 접지 않으면 긴 실행에서 시간이 끊긴다(감사 C-12). 주기마다 한 번 시간이 0 으로 돌아간다 — Godot `time_rollover_secs` 와 같은 값이다.
         */
        PROPERTY( Min = 1, Units = s )
        float64 _shaderTimeRolloverSeconds{};

        SW_SERVICE_SLOT_CONST( EngineConfig, Required );
    };
} // namespace sw
