/**
 * @file RhythmChart.h
 * @brief 건반 리듬 게임의 채보 — 노트(레인 · 박 · 롱노트 끝 박) · BPM 변화 · 정지 · 변박과 박 ↔ 초 변환, 화면 스크롤 위치입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 노트가 판정선으로 내려오는 빠르기를 정하는 방식입니다. */
    enum class RhythmScrollMode : uint8
    {
        ConstantSpeed = 0, ///< 시간 기반 — BPM 이 바뀌어도 내려오는 빠르기가 같다(정지에서도 계속 내려온다)
        FollowBpm          ///< 박 기반 — 빠른 구간은 빨리, 정지에서는 멈춘다(비트매니아 기본)
    };

    /** @brief 노트 하나입니다. 시각은 채보를 읽을 때 박에서 계산해 둡니다(채보 오프셋 포함, 음악 시각). */
    struct RhythmNote
    {
        float32 _beat{ 0.0f };
        float32 _endBeat{ 0.0f }; ///< 롱노트의 끝 박(보통 노트는 `_beat` 와 같다)
        float32 _time{ 0.0f };    ///< 초
        float32 _endTime{ 0.0f };
        int32   _lane{ 0 };
        uint8   _bLong{ SW_FALSE };
    };

    /** @brief BPM 변화입니다. */
    struct RhythmBpmChange
    {
        float32 _beat{ 0.0f };
        float32 _bpm{ 120.0f };
    };

    /** @brief 정지입니다 — 이 박에서 음악은 흐르지만 박은 `_seconds` 동안 멈춥니다. */
    struct RhythmStop
    {
        float32 _beat{ 0.0f };
        float32 _seconds{ 0.0f };
    };

    /** @brief 변박입니다 — 이 박에서 새 마디가 시작되고 마디 길이가 `_beatsPerMeasure` 박이 됩니다. */
    struct RhythmMeasureChange
    {
        float32 _beat{ 0.0f };
        float32 _beatsPerMeasure{ 4.0f };
    };

    /**
     * @brief 박 ↔ 초 표의 한 점입니다. 이 박부터 다음 점까지 박마다 `_secondsPerBeat` 초이고, 이 박에서 먼저 `_stopSeconds` 만큼 멈춥니다.
     * @details 이 박 위에 놓인 노트는 정지가 시작될 때(`_time`) 칩니다(스텝매니아 · BMS 와 같다).
     */
    struct RhythmTimingPoint
    {
        float32 _beat{ 0.0f };
        float32 _time{ 0.0f };
        float32 _secondsPerBeat{ 0.5f };
        float32 _stopSeconds{ 0.0f };
    };

    /**
     * @class RhythmChart
     * @brief `<Chart title=".." artist=".." level="12" lanes="7" offset="0.05"><Bpm beat="0" bpm="140"/><Stop beat="16" seconds="0.5"/>
     *        <Measure beat="32" beatsPerMeasure="3"/><Note lane="0" beat="1.5"/><Note lane="3" beat="4" endBeat="6"/></Chart>` 를 읽습니다.
     * @details `offset` 은 박 0 이 오는 음악 시각(초)입니다. BPM 변화 · 정지는 읽을 때 박 순으로 적분해 표(`RhythmTimingPoint`)로 두고,
     *          박 → 초 · 초 → 박은 그 표를 이분 탐색합니다. 노트는 시각 · 레인 순으로 정렬됩니다. 같은 레인에서 앞 롱노트가 끝나기 전에 시작하는 노트는
     *          경고하고 버립니다. 엔진 없이 쓰는 보통 클래스입니다.
     */
    class SW_GF_API RhythmChart
    {
    public:
        RhythmChart();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        /** @brief 박의 음악 시각(초)입니다. 박 0 보다 앞은 첫 BPM 으로 늘입니다. */
        float32 convertBeatToSeconds( float32 beat ) const;
        /** @brief 음악 시각(초)의 박입니다. 정지 동안은 정지한 박 그대로입니다. */
        float32 convertSecondsToBeat( float32 seconds ) const;
        /** @brief 그 박의 BPM 입니다. */
        float32 findBpmAt( float32 beat ) const;

        /**
         * @brief 노트가 판정선 위로 얼마나 떨어져 있는지입니다(판정선 = 0, 위가 +). @p hiSpeed 는 "기준 BPM 에서 1 초 뒤 노트의 거리" 입니다.
         * @details `ConstantSpeed` 는 (노트 시각 − 지금) × hiSpeed, `FollowBpm` 은 (노트 박 − 지금 박) × 기준 BPM 의 박 길이 × hiSpeed 입니다.
         *          기준 BPM(`baseBpm`, 없으면 첫 BPM)의 구간에서는 두 방식이 같은 자리를 냅니다.
         */
        float32 computeNoteY( float32 noteBeat, float32 currentTime, float32 hiSpeed, RhythmScrollMode mode = RhythmScrollMode::ConstantSpeed ) const;

        /** @brief 박 0 부터 @p endBeat 까지 마디선의 박을 채웁니다(변박 반영 · 기본 4 박). */
        void fillMeasureLineBeats( float32 endBeat, vector<float32>& outListBeat ) const;

        const vector<RhythmNote>&          getNotes() const { return _listNote; }
        const vector<RhythmTimingPoint>&   getTimingPoints() const { return _listTimingPoint; }
        const vector<RhythmMeasureChange>& getMeasureChanges() const { return _listMeasureChange; }
        const string&                      getTitle() const { return _title; }
        const string&                      getArtist() const { return _artist; }
        float32                            getOffset() const { return _offset; }
        float32                            getBaseBpm() const { return _baseBpm; }
        int32                              getLevel() const { return _level; }
        int32                              getLaneCount() const { return _laneCount; }
        /** @brief 판정 수 — 노트마다 하나, 롱노트는 머리와 끝 둘입니다. */
        int32 getJudgmentCount() const { return _judgmentCount; }
        /** @brief 마지막 노트(롱노트는 끝)의 시각입니다. */
        float32 getEndTime() const { return _endTime; }

    private:
        [[nodiscard]] bool loadRoot( const XmlNode& root, string_view sourceName );
        void               makeTimingPoints( const vector<RhythmBpmChange>& listBpmChange, const vector<RhythmStop>& listStop );

        vector<RhythmNote>          _listNote;
        vector<RhythmTimingPoint>   _listTimingPoint; ///< 박 순(곧 시각 순)
        vector<RhythmMeasureChange> _listMeasureChange;
        string                      _title;
        string                      _artist;
        float32                     _offset;
        float32                     _baseBpm;
        float32                     _endTime;
        int32                       _level;
        int32                       _laneCount;
        int32                       _judgmentCount;
    };
} // namespace sw
