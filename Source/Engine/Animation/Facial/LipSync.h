/**
 * @file LipSync.h
 * @brief 립싱크입니다. 음성 클립을 진폭과 주파수 대역 에너지로 나눠 비즘(viseme) 가중치 트랙으로 분석합니다(오프라인 임포트). 트랙이 없으면 진폭만 사용합니다(런타임).
 * @details 언리얼 립싱크 플러그인(OVR LipSync, 진폭 기반)에 해당한다. 단어 단위 음소 인식(음향 모델)은 하지 않는다. 대역 에너지 형태를 데이터의 비즘 대역 테이블과
 *          비교하고, 유성 프레임은 선형 예측으로 잰 두 포먼트를 모음 비즘의 포먼트(`LipSyncViseme::_formantHz`)와 견줘 모음을 가른다.
 *          비즘에서 모프 타깃으로 가는 연결은 얼굴 리그(`FacialRig`)가 데이터로 정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct LipSyncViseme
     * @brief 분석이 고르는 비즘 하나와 그 대역 형태(저, 중, 고 에너지 비율)입니다.
     */
    REFLECT()
    struct SW_API LipSyncViseme
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Viseme name - the facial rig maps it to morph targets" )
        hashed_string _name;
        PROPERTY( Tooltip = "Energy ratio of the low, mid and high band (x, y, z) this viseme looks like" )
        float3 _bandShape{ 0.0f, 0.0f, 0.0f };
        PROPERTY( Units = Hz, Tooltip = "First and second formant of a vowel viseme (0,0: not a vowel - the band shape alone decides)" )
        float2 _formantHz{ 0.0f, 0.0f };

        bool isVowel() const { return _formantHz._x > 0.0f && _formantHz._y > 0.0f; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LipSyncSettings
     * @brief 분석 표(데이터, `engine/animation/lipsync.xml`)입니다. 첫 비즘은 무음(sil)으로 씁니다.
     * @details 파일은 리플렉션 구조체로 읽는다(`ConfigManager::readConfigXMLElement` — 모르는 속성 · 자식 · 읽지 못한 값 · 범위 밖 숫자는 오류). 칸 표는 PROPERTY 가 정본이다.
     *          대역은 (낮은 Hz, 높은 Hz) 쌍이고, 비즘은 둘 이상이며 `_fallbackViseme` 은 그 가운데 하나여야 한다(`validate`).
     */
    REFLECT()
    struct SW_API LipSyncSettings
    {
        REFLECT_BODY();

        /** @brief 테이블 파일의 리소스 경로입니다. */
        static constexpr string_view kResourcePath = "engine/animation/lipsync.xml";

        PROPERTY( Tooltip = "Visemes the analysis picks from (at least two; the first one is silence)" )
        vector<LipSyncViseme> _listViseme;
        PROPERTY( Units = Hz, Tooltip = "Low band pass range (low, high)" )
        float2 _lowBandHz{ 150.0f, 600.0f };
        PROPERTY( Units = Hz, Tooltip = "Mid band pass range (low, high)" )
        float2 _midBandHz{ 600.0f, 1600.0f };
        PROPERTY( Units = Hz, Tooltip = "High band pass range (low, high)" )
        float2 _highBandHz{ 1600.0f, 7000.0f };
        PROPERTY( Min = 0.0, Units = fps, Tooltip = "Viseme track frames per second (greater than 0)" )
        float32 _frameRate{ 30.0f };
        PROPERTY( Min = 0.0, Tooltip = "RMS below this is silence" )
        float32 _silenceRms{ 0.02f };
        PROPERTY( Min = 0.0, Tooltip = "RMS at which the mouth is fully open (above the silence RMS)" )
        float32 _fullRms{ 0.25f };
        PROPERTY( Min = 0.0, Tooltip = "Sharpness of the band shape comparison (inverse softmax temperature)" )
        float32 _sharpness{ 8.0f };
        PROPERTY( Min = 0.0, Tooltip = "Score a vowel viseme gains when the frame's measured formants match its _formantHz (voiced frames only)" )
        float32 _formantWeight{ 6.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "High band energy ratio above which a frame is unvoiced (hiss) and formants are not compared" )
        float32 _unvoicedHighBandRatio{ 0.5f };
        PROPERTY( Tooltip = "Viseme the amplitude drives when there is no track" )
        hashed_string _fallbackViseme;

        /** @brief XML 을 읽고 검사합니다. 틀리면 오류를 남기고 false 이며, 내용은 기본값으로 돌아갑니다. */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 프레임 속도 · RMS 순서 · 대역 순서 · 비즘 수 · 대체 비즘을 봅니다. 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate( string_view sourceLabel ) const;
        /** @brief 비즘 번호입니다. 없으면 -1 입니다. */
        int32 findVisemeIndex( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VisemeTrack
     * @brief 비즘 가중치 트랙(곡선 묶음)입니다. 프레임 × 비즘 가중치이며, `<음성>.visemes.xml` 로 저장합니다(`App --import-lipsync`, `LipSyncImport`).
     * @details 형식은 `<VisemeTrack _frameRate="30"><_listViseme><item>이름</item>…</_listViseme><_listFrame><Frame>가중치,가중치,…</Frame>…</_listFrame></VisemeTrack>` 이다.
     */
    struct SW_API VisemeTrack
    {
        /** @brief 트랙 파일 확장자입니다(음성 파일의 확장자를 이것으로 바꾼다). */
        static constexpr string_view kExtension = ".visemes.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `LipSync.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "VisemeTrack", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        vector<hashed_string> _listViseme;
        vector<float32>       _listWeight; ///< 프레임 우선(`[frame * visemeCount + viseme]`)
        float32               _frameRate{ 30.0f };

        uint32  getVisemeCount() const { return static_cast<uint32>( _listViseme.size() ); }
        uint32  getFrameCount() const { return _listViseme.empty() ? 0u : static_cast<uint32>( _listWeight.size() / _listViseme.size() ); }
        float32 getDuration() const { return _frameRate > 0.0f ? static_cast<float32>( getFrameCount() ) / _frameRate : 0.0f; }
        /** @brief 시각 @p time(초)의 비즘 가중치들입니다(두 프레임 사이 선형). @p outListWeight 는 비즘 수입니다. 끝을 지나면 모두 0 입니다. */
        void sample( float32 time, vector<float32>& outListWeight ) const;

        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        [[nodiscard]] bool loadFromResource( string_view path );
        string             toXML() const;
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief 음성 경로의 트랙 경로입니다(`a/line.wav` → `a/line.visemes.xml`). */
        static string makePathForAudio( string_view audioPath );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LipSyncAnalyzer
     * @brief 음성 분석입니다. 프레임마다 RMS 와 세 대역(바이쿼드 대역 통과) 에너지를 재고, 대역 형태를 비즘 테이블과 비교해 가중치를 냅니다.
     * @details 입력은 모노 float 샘플이다. 소리 파일을 디코드하는 일(`AudioClipData::copyMonoSamples`)과 일괄 임포트(`LipSyncImport`)는 Audio 쪽에 있다.
     *          Animation 은 Audio 보다 아래 티어이기 때문이다.
     */
    struct SW_API LipSyncAnalyzer
    {
        /** @brief 모노 표본을 트랙으로 분석합니다. */
        static void analyze( const vector<float32>& listSample, uint32 sampleRate, const LipSyncSettings& settings, VisemeTrack& outTrack );
        /** @brief 시각 @p time 근처(한 프레임 창)의 RMS 를 반환합니다. 트랙이 없을 때 런타임 진폭 립싱크가 씁니다. */
        static float32 computeRms( const vector<float32>& listSample, uint32 sampleRate, float32 time, float32 windowSeconds );
        /** @brief RMS 를 입 벌림 [0, 1] 로 바꿉니다(무음 아래 0, full 위 1). */
        static float32 computeOpenness( float32 rms, const LipSyncSettings& settings );
        /**
         * @brief 표본 한 창의 첫째 · 둘째 포먼트(Hz)를 잽니다 — 선형 예측(LPC) 포락선의 처음 두 봉우리입니다. 봉우리가 둘 없으면 false 입니다.
         * @details 높은 표본율은 약 16 kHz 로 평균 솎아 내고(포먼트는 4 kHz 아래다), 프리엠퍼시스 · 해밍 창 · 자기상관 · Levinson-Durbin 을 거친다(Praat 의 Burg 대신 자기상관법).
         */
        [[nodiscard]] static bool tryComputeFormants( const float32* pSample, size_t sampleCount, uint32 sampleRate, float2& outFormantHz );
    };
} // namespace sw
