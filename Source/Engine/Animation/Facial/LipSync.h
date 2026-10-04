/**
 * @file LipSync.h
 * @brief 립싱크 — 음성 클립을 진폭 · 주파수 대역 에너지로 나눠 비즘(viseme) 가중치 트랙으로 분석합니다(오프라인 임포트), 트랙이 없으면 진폭만으로(런타임).
 * @details 언리얼 립싱크 플러그인(OVR LipSync · 진폭 기반)과 같은 자리입니다. 음소 인식은 하지 않습니다 — 대역 에너지 모양을 데이터의 비즘 대역 표와
 *          견주는 가벼운 분류입니다. 비즘 → 모프 타깃은 얼굴 리그(`FacialRig`)가 데이터로 잇습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class JsonValue;

    /**
     * @struct LipSyncViseme
     * @brief 분석이 고르는 비즘 하나와 그 대역 모양(저 · 중 · 고 에너지 비율)입니다.
     */
    struct LipSyncViseme
    {
        hashed_string _name;
        float32       _arrBand[3]{ 0.0f, 0.0f, 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LipSyncSettings
     * @brief 분석 표(데이터, `engine/animation/lipsync.json`): `{ "frame_rate", "silence_rms", "full_rms", "sharpness", "fallback_viseme",
     *        "bands": [ [저 Hz, 고 Hz] x 3 ], "visemes": [ { "name", "bands": [저, 중, 고] } ] }`. 첫 비즘은 무음(sil)으로 씁니다.
     */
    struct SW_API LipSyncSettings
    {
        /** @brief 표 파일의 리소스 경로입니다. */
        static constexpr string_view kResourcePath = "engine/animation/lipsync.json";

        vector<LipSyncViseme> _listViseme;
        float32               _arrBandRange[3][2]{
            { 150.0f,  600.0f},
            { 600.0f, 1600.0f},
            {1600.0f, 7000.0f}
        };
        float32       _frameRate{ 30.0f };
        float32       _silenceRms{ 0.02f }; ///< 이보다 작으면 무음
        float32       _fullRms{ 0.25f };    ///< 이 크기에서 입이 다 열린다
        float32       _sharpness{ 8.0f };   ///< 대역 모양 비교의 날카로움(softmax 온도의 역)
        hashed_string _fallbackViseme;      ///< 트랙이 없을 때 진폭이 움직이는 비즘

        [[nodiscard]] bool parseJson( string_view json, string_view sourceLabel );
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 비즘 번호입니다. 없으면 -1 입니다. */
        int32 findVisemeIndex( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool parseRoot( const JsonValue& root, string_view sourceLabel );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct VisemeTrack
     * @brief 비즘 가중치 트랙(곡선 묶음) — 프레임 × 비즘 가중치입니다. `<음성>.visemes.json` 으로 저장합니다(`App --import-lipsync` — `LipSyncImport`).
     * @details 형식: `{ "frame_rate", "visemes": [ 이름 ... ], "frames": [ [ 가중치 ... ] ... ] }`.
     */
    struct SW_API VisemeTrack
    {
        /** @brief 트랙 파일 확장자입니다(음성 파일의 확장자를 이것으로 바꾼다). */
        static constexpr string_view kExtension = ".visemes.json";

        vector<hashed_string> _listViseme;
        vector<float32>       _listWeight; ///< 프레임 우선(`[frame * visemeCount + viseme]`)
        float32               _frameRate{ 30.0f };

        uint32  getVisemeCount() const { return static_cast<uint32>( _listViseme.size() ); }
        uint32  getFrameCount() const { return _listViseme.empty() ? 0u : static_cast<uint32>( _listWeight.size() / _listViseme.size() ); }
        float32 getDuration() const { return _frameRate > 0.0f ? static_cast<float32>( getFrameCount() ) / _frameRate : 0.0f; }
        /** @brief 시각 @p time(초)의 비즘 가중치들입니다(두 프레임 사이 선형). @p outListWeight 는 비즘 수입니다. 끝을 지나면 모두 0 입니다. */
        void sample( float32 time, vector<float32>& outListWeight ) const;

        [[nodiscard]] bool parseJson( string_view json, string_view sourceLabel );
        [[nodiscard]] bool loadFromResource( string_view path );
        string             toJson() const;
        [[nodiscard]] bool saveToFile( string_view path ) const;
        /** @brief 음성 경로의 트랙 경로입니다(`a/line.wav` → `a/line.visemes.json`). */
        static string makePathForAudio( string_view audioPath );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LipSyncAnalyzer
     * @brief 음성 분석입니다 — 프레임마다 RMS 와 세 대역(바이쿼드 대역 통과) 에너지를 재고, 대역 모양을 비즘 표와 견줘 가중치를 냅니다.
     * @details 입력은 모노 float 표본입니다. 소리 파일을 푸는 일(`AudioClipData::copyMonoSamples`)과 일괄 임포트(`LipSyncImport`)는 Audio 쪽입니다 —
     *          Animation 은 Audio 아래 티어입니다.
     */
    struct SW_API LipSyncAnalyzer
    {
        /** @brief 모노 표본을 트랙으로 분석합니다. */
        static void analyze( const vector<float32>& listSample, uint32 sampleRate, const LipSyncSettings& settings, VisemeTrack& outTrack );
        /** @brief 시각 @p time 근처(한 프레임 창)의 RMS 입니다 — 트랙이 없을 때 런타임 진폭 립싱크가 씁니다. */
        static float32 computeRms( const vector<float32>& listSample, uint32 sampleRate, float32 time, float32 windowSeconds );
        /** @brief RMS 를 입 벌림 [0, 1] 로 바꿉니다(무음 아래 0, full 위 1). */
        static float32 computeOpenness( float32 rms, const LipSyncSettings& settings );
    };
} // namespace sw
