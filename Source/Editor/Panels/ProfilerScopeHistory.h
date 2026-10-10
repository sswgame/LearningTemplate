/**
 * @file ProfilerScopeHistory.h
 * @brief 에디터 프로파일러 패널의 상태 · 집계입니다(ImGui 없음 — `EditorTest` 가 시험한다). 그리기는 `ProfilerPanel` 입니다.
 * @details 엔진 `FrameProfiler` 는 세션 전체를 접은 통계(`-gv_profileFrames` 보고)를 듭니다. 패널은 **최근 N 프레임**을 실시간으로 봐야 하므로
 *          프레임마다 마지막으로 접힌 값(`getLastFrameNanos` · `getLastFrameCount`)을 구간별 고리에 담아 창 안에서 p50 · p99 · 최대를 셉니다.
 *          Tracy 가 꺼진 빌드에서도 같습니다 — 이 표는 엔진 표만 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class FrameProfiler;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum ProfilerScopeKind
     * @brief 구간의 종류입니다. 표를 나누는 기준입니다.
     */
    enum class ProfilerScopeKind : uint8
    {
        CPU,     ///< 시간 구간(`SW_PROFILE_SCOPE`) — 값은 마이크로초
        GPU,     ///< GPU 타임스탬프 구간(`GPU.<패스>`) — 값은 마이크로초
        Counter, ///< 카운터(`SW_PROFILE_COUNT`) — 값은 프레임당 합
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @enum ProfilerSortColumn
     * @brief 표를 정렬하는 열입니다.
     */
    enum class ProfilerSortColumn : uint8
    {
        Name,
        Last,
        Average,
        P50,
        P99,
        Max,
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 표 한 줄입니다. 값의 단위는 종류를 따릅니다(시간 = 마이크로초, 카운터 = 개수). 표본이 없는 값은 음수입니다. */
    struct ProfilerScopeRow
    {
        string            _name;
        float64           _last;    ///< 가장 최근 프레임
        float64           _average; ///< 창 안 표본 평균
        float64           _p50;
        float64           _p99;
        float64           _max;
        uint32            _sampleCount; ///< 창 안에서 불린 프레임 수
        ProfilerScopeKind _kind;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 표를 만드는 조건입니다(종류 · 정렬 · 검색어). */
    struct ProfilerRowQuery
    {
        string             _filterText;                            ///< 이름에 들어 있어야 하는 글자(대소문자 무시). 비면 모두
        ProfilerScopeKind  _kind{ ProfilerScopeKind::CPU };        ///< 이 종류만
        ProfilerSortColumn _sortColumn{ ProfilerSortColumn::P99 }; ///< 정렬 열
        bool               _bDescending{ true };                   ///< 큰 값이 위
        uint32             _frameOffset{ 0 };                      ///< "Last" 열이 보일 프레임(0 = 가장 최근, 1 = 그 앞 …) — 그래프에서 고른 프레임
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ProfilerScopeHistory
     * @brief 구간마다 최근 N 프레임 값을 고리로 들고, 창 안 통계 · 정렬 · 검색 · 그래프 값을 냅니다.
     */
    class ProfilerScopeHistory
    {
    public:
        /** @brief 기본 창 크기(프레임)입니다. 60 fps 에서 4 초입니다. */
        static constexpr uint32 kDefaultWindowFrame = 240;

        explicit ProfilerScopeHistory( uint32 windowFrame = kDefaultWindowFrame );

        /**
         * @brief 프로파일러가 새 프레임을 접었으면 그 값을 한 칸 담습니다.
         * @return 담았으면 true. 프레임 수가 그대로면(같은 프레임에 두 번 그리거나 프로파일러가 꺼져 있으면) false 입니다.
         */
        bool capture( const FrameProfiler& profiler );
        /** @brief 담은 값을 모두 비웁니다(창 크기는 그대로). */
        void reset();

        /** @brief 조건에 맞는 줄을 만들어 정렬합니다. */
        void makeRows( const ProfilerRowQuery& query, vector<ProfilerScopeRow>& outListRow ) const;
        /**
         * @brief 이름 붙은 구간의 최근 값을 오래된 것부터 담습니다(그래프용). 안 불린 프레임은 0 입니다.
         * @return 그런 구간이 없으면 false 입니다.
         */
        [[nodiscard]] bool copySeries( const string& scopeName, vector<float32>& outListValue ) const;
        /** @brief 이름 붙은 구간의 @p frameOffset 프레임 전 값입니다(0 = 가장 최근). 그 프레임에 안 불렸거나 구간이 없으면 false 입니다. */
        [[nodiscard]] bool readValue( const string& scopeName, uint32 frameOffset, float32& outValue ) const;

        /**
         * @brief 담은 창을 글 파일로 씁니다(프로파일 캡처 저장). 구간마다 한 줄 — 종류, 이름, 오래된 것부터 창 크기만큼의 값(안 불린 칸은 -1).
         * @details 언리얼 `stat startfile` · 유니티 Profiler 의 Save 자리다. 깊은 분석(시간축 · 스레드)은 Tracy 캡처가 맡고, 이것은 패널 표를 다시 보는 용도다.
         */
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
        /** @brief `saveToFile` 이 쓴 파일을 읽어 창을 바꿉니다. 형식이 틀리면 지금 값을 그대로 두고 false 입니다. */
        [[nodiscard]] bool loadFromFile( string_view filePath );

        /** @brief 창 크기(프레임)입니다. */
        uint32 getWindowFrame() const { return _windowFrame; }
        /** @brief 지금까지 담은 프레임 수입니다(창 크기를 넘으면 오래된 것부터 덮는다). */
        uint64 getCapturedFrameCount() const { return _capturedFrameCount; }

    private:
        /** @brief 구간 하나의 고리입니다. 안 불린 프레임은 음수입니다. */
        struct ScopeTrack
        {
            string            _name;
            vector<float32>   _listValue; ///< 크기 = 창 크기, `_writeIndex` 가 다음 칸
            ProfilerScopeKind _kind{ ProfilerScopeKind::CPU };
        };

    private:
        /** @brief 이름의 종류를 정합니다. 카운터 표시가 먼저이고, 이름이 `GPU.` 로 시작하면 GPU 입니다. */
        static ProfilerScopeKind classifyScope( const string& name, bool bCounter );
        /** @brief 트랙 하나의 창 안 통계를 줄에 채웁니다. "Last" 는 @p frameOffset 프레임 전 값입니다. */
        void fillRow( const ScopeTrack& track, uint32 frameOffset, ProfilerScopeRow& outRow ) const;

    private:
        vector<ScopeTrack> _listTrack;          ///< 프로파일러 슬롯 번호 = 인덱스
        uint64             _capturedFrameCount; ///< 담은 프레임 수
        uint64             _lastProfilerFrame;  ///< 마지막으로 담은 프로파일러 프레임 번호
        uint32             _windowFrame;        ///< 고리 크기
        uint32             _writeIndex;         ///< 다음에 쓸 칸
    };
} // namespace sw::editor
