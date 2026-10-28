/**
 * @file ProfilerScopeHistory.h
 * @brief 에디터 프로파일러 패널의 상태와 집계를 담당합니다. ImGui 에 의존하지 않으며 `EditorTest` 가 테스트합니다. 그리기는 `ProfilerPanel` 이 합니다.
 * @details 엔진의 `FrameProfiler` 는 세션 전체를 합친 통계(`-gv_profileFrames` 보고)를 보관한다. 패널은 최근 N 프레임을 실시간으로 보여 줘야 하므로
 *          프레임마다 마지막으로 집계된 값(`getLastFrameNanos`, `getLastFrameCount`)을 구간별 링 버퍼에 담고, 창 안에서 p50, p99, 최대를 계산한다.
 *          이 클래스는 엔진 테이블만 읽으므로 Tracy 가 꺼진 빌드에서도 같게 동작한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class FrameProfiler;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum ProfilerScopeKind
     * @brief 구간의 종류입니다. 테이블을 나누는 기준입니다.
     */
    enum class ProfilerScopeKind : uint8
    {
        CPU,     ///< 시간 구간(`SW_PROFILE_SCOPE`). 값의 단위는 마이크로초
        GPU,     ///< GPU 타임스탬프 구간(`GPU.<패스>`). 값의 단위는 마이크로초
        Counter, ///< 카운터(`SW_PROFILE_COUNT`). 값은 프레임당 합계
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @enum ProfilerSortColumn
     * @brief 테이블을 정렬하는 열입니다.
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
    /** @brief 테이블의 행 하나입니다. 값의 단위는 종류를 따르며, 시간은 마이크로초이고 카운터는 개수입니다. 샘플이 없는 값은 음수입니다. */
    struct ProfilerScopeRow
    {
        string            _name;
        float64           _last;    ///< 가장 최근 프레임
        float64           _average; ///< 창 안 샘플의 평균
        float64           _p50;
        float64           _p99;
        float64           _max;
        uint32            _sampleCount; ///< 창 안에서 호출된 프레임 수
        ProfilerScopeKind _kind;
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 테이블을 만드는 조건입니다. 종류, 정렬, 검색어로 이루어집니다. */
    struct ProfilerRowQuery
    {
        string             _filterText;                            ///< 이름에 들어 있어야 하는 문자열(대소문자 무시). 비어 있으면 모두 통과한다
        ProfilerScopeKind  _kind{ ProfilerScopeKind::CPU };        ///< 이 종류만 보여 준다
        ProfilerSortColumn _sortColumn{ ProfilerSortColumn::P99 }; ///< 정렬 열
        bool               _bDescending{ true };                   ///< 큰 값이 위에 온다
        uint32             _frameOffset{ 0 };                      ///< "Last" 열이 보여 줄 프레임이다. 0 이 가장 최근이고 1 이 그 앞 프레임이며, 그래프에서 고른 프레임이다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ProfilerScopeHistory
     * @brief 구간마다 최근 N 프레임 값을 링 버퍼에 보관하고, 창 안의 통계와 정렬, 검색, 그래프 값을 제공합니다.
     */
    class ProfilerScopeHistory
    {
    public:
        /** @brief 기본 창 크기(프레임)입니다. 60 fps 에서 4초입니다. */
        static constexpr uint32 kDefaultWindowFrame = 240;

        explicit ProfilerScopeHistory( uint32 windowFrame = kDefaultWindowFrame );

        /**
         * @brief 프로파일러가 새 프레임을 집계했으면 그 값을 한 항목 담습니다.
         * @return 담았으면 true 입니다. 프레임 수가 그대로이면 false 이며, 같은 프레임에 두 번 그리거나 프로파일러가 꺼져 있을 때가 해당합니다.
         */
        bool capture( const FrameProfiler& profiler );
        /** @brief 담은 값을 모두 비웁니다. 창 크기는 그대로입니다. */
        void reset();

        /** @brief 조건에 맞는 행을 만들어 정렬합니다. */
        void makeRows( const ProfilerRowQuery& query, vector<ProfilerScopeRow>& outListRow ) const;
        /**
         * @brief 이름이 붙은 구간의 최근 값을 오래된 것부터 담습니다. 그래프에 쓰며, 호출되지 않은 프레임은 0 입니다.
         * @return 그런 구간이 없으면 false 입니다.
         */
        [[nodiscard]] bool copySeries( const string& scopeName, vector<float32>& outListValue ) const;
        /** @brief 이름이 붙은 구간의 @p frameOffset 프레임 전 값을 구합니다. 0 이 가장 최근입니다. 그 프레임에 호출되지 않았거나 구간이 없으면 false 입니다. */
        [[nodiscard]] bool readValue( const string& scopeName, uint32 frameOffset, float32& outValue ) const;

        /**
         * @brief 담은 창을 텍스트 파일로 저장합니다(프로파일 캡처 저장). 구간마다 한 줄이며 종류, 이름, 오래된 것부터 창 크기만큼의 값을 씁니다. 호출되지 않은 칸은 -1 입니다.
         * @details 언리얼의 `stat startfile`, 유니티 Profiler 의 Save 에 해당한다. 깊은 분석(시간축, 스레드)은 Tracy 캡처가 맡고, 이 파일은 패널 테이블을 다시 보는 용도다.
         */
        [[nodiscard]] bool saveToFile( string_view filePath ) const;
        /** @brief `saveToFile` 이 쓴 파일을 읽어 창을 교체합니다. 형식이 틀리면 현재 값을 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] bool loadFromFile( string_view filePath );

        /** @brief 창 크기(프레임)입니다. */
        uint32 getWindowFrame() const { return _windowFrame; }
        /** @brief 지금까지 담은 프레임 수입니다. 창 크기를 넘으면 오래된 것부터 덮어씁니다. */
        uint64 getCapturedFrameCount() const { return _capturedFrameCount; }

    private:
        /** @brief 구간 하나의 링 버퍼입니다. 호출되지 않은 프레임은 음수입니다. */
        struct ScopeTrack
        {
            string            _name;
            vector<float32>   _listValue; ///< 크기는 창 크기이며, `_writeIndex` 가 다음에 쓸 위치다
            ProfilerScopeKind _kind{ ProfilerScopeKind::CPU };
        };

    private:
        /** @brief 이름으로 종류를 정합니다. 카운터 표시를 먼저 보고, 이름이 `GPU.` 로 시작하면 GPU 입니다. */
        static ProfilerScopeKind classifyScope( const string& name, bool bCounter );
        /** @brief 트랙 하나의 창 안 통계를 행에 채웁니다. "Last" 는 @p frameOffset 프레임 전의 값입니다. */
        void fillRow( const ScopeTrack& track, uint32 frameOffset, ProfilerScopeRow& outRow ) const;

    private:
        vector<ScopeTrack> _listTrack;          ///< 인덱스가 프로파일러 슬롯 번호다
        uint64             _capturedFrameCount; ///< 담은 프레임 수
        uint64             _lastProfilerFrame;  ///< 마지막으로 담은 프로파일러 프레임 번호
        uint32             _windowFrame;        ///< 링 버퍼 크기
        uint32             _writeIndex;         ///< 다음에 쓸 위치
    };
} // namespace sw::editor
