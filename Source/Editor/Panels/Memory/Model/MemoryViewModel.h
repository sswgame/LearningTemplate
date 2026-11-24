/**
 * @file MemoryViewModel.h
 * @brief Memory 창의 ViewModel — 스냅숏(`MemorySnapshot`)을 찍어 들고, 고른 시트를 정렬 · 이름 거르기 · 형식 거르기한 표나 두 스냅숏의 비교 표로 짓고(`gather`),
 *        표 · 스냅숏 CSV 내보내기를 워커에 냅니다. ImGui 를 모릅니다.
 * @details 원천은 이 프로세스(편집 씬 · PIE 씬이 같은 프로세스 — `MemorySnapshotSources::makeEngine`)와, Multiplayer 창 Inspector 탭이 붙은 원격 서버(디버그 채널의
 *          개발 명령 `memory.snapshot`)다. 패널(`MemoryPanel`)은 표를 그리고 에셋 줄을 콘텐츠 브라우저로 보낼 뿐이다(MVVM 계약). 상용 엔진 비교: 유니티 Memory Profiler
 *          (스냅숏 목록 · 표 · Compare), 언리얼 Memory Insights 의 두 시점 비교. 스냅숏은 디스크 문서가 아니라 되돌리기에 남지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

#include "DevTools/Profiling/Memory/MemorySnapshot.h"
#include "DevTools/Profiling/Memory/MemorySnapshotExport.h"

#include "Editor/Common/Task/EditorBackgroundTask.h"

namespace sw::editor
{
    /** @brief "스냅숏 없음" 번호입니다(보는 스냅숏 · 비교의 앞). */
    inline constexpr int32 kMemoryNoSnapshot = -1;

    /** @brief 요약의 태그 막대 하나입니다(살아 있는 바이트가 큰 태그부터). */
    struct MemoryTagBar
    {
        string  _name{};
        string  _bytesText{};
        float32 _fraction{ 0.0f }; ///< 태그 합에 대한 몫(0 ~ 1)
    };

    /** @brief 표의 칸 머리입니다. */
    struct MemoryTableColumn
    {
        string _name{};
        uint8  _bNumeric{ SW_FALSE }; ///< 숫자 칸 — 오른쪽 정렬(표 숫자)
    };

    /** @brief 표 한 줄입니다(그릴 글만). */
    struct MemoryTableRow
    {
        vector<string>   _listCell{};
        string           _assetPath{}; ///< 에셋 시트의 경로(콘텐츠 브라우저로 갈 수 있다). 다른 시트는 비어 있다
        MemoryDiffStatus _status{};    ///< 비교 표의 상태(비교 표를 지을 때 채운다 — 비교가 아니면 쓰지 않는다)
    };

    /** @brief Memory 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct MemoryViewState
    {
        vector<string>            _listSnapshotLabel{};
        int32                     _currentIndex{ kMemoryNoSnapshot }; ///< 보는 스냅숏(없으면 -1)
        int32                     _compareIndex{ kMemoryNoSnapshot }; ///< 비교의 앞 스냅숏(비교하지 않으면 -1)
        string                    _cpuText{};                         ///< CPU 태그 합
        string                    _gpuText{};                         ///< GPU 자원 합
        string                    _gpuDescription{};                  ///< GPU 백엔드 · 바이트 기준
        string                    _source{};                          ///< 원천(local · 원격 주소)
        vector<MemoryTagBar>      _listTagBar{};
        MemorySheetKind           _sheet{ MemorySheetKind::Tags };
        vector<MemoryTableColumn> _listColumn{};
        vector<MemoryTableRow>    _listRow{};
        uint32                    _sheetRowCount{ 0 }; ///< 거르기 전 줄 수
        uint32                    _sortColumn{ 0 };
        uint8                     _bSortDescending{ SW_TRUE };
        uint8                     _bCompareMode{ SW_FALSE };
        uint8                     _bRemoteAvailable{ SW_FALSE }; ///< Multiplayer 창 Inspector 가 원격 서버에 붙어 있다
        uint8                     _bExportPending{ SW_FALSE };
        vector<string>            _listFilterOption{}; ///< 형식 거르기 값(0 번은 모두). 거르기 칸이 없는 시트는 비어 있다
        uint32                    _filterIndex{ 0 };
        string                    _statusLine{}; ///< 마지막 내보내기 · 원격 결과
    };

    /** @brief 내보내기 잡 입력입니다 — 스냅숏 전체(`_bWholeSnapshot`) 또는 표 하나(`_table` · `_fileName`). */
    struct MemoryExportRequest
    {
        shared_ptr<const MemorySnapshot> _pSnapshot{};
        CSVTable                         _table{};
        string                           _fileName{};
        AbsolutePath                     _folder{};
        uint8                            _bWholeSnapshot{ SW_FALSE };
    };

    /** @brief 내보내기 잡 결과입니다. */
    struct MemoryPanelExportResult
    {
        string _folder{};
        uint32 _fileCount{ 0 };
        string _error{}; ///< 실패면 이유
    };

    /** @class MemoryExportJob @brief CSV 를 워커에서 씁니다(큰 표의 글 짓기 · 쓰기가 UI 를 막지 않게). 에디터 백그라운드 잡이라 시나리오의 정지 대기가 끝을 기다린다. */
    class MemoryExportJob final : public EditorBackgroundTask<MemoryExportRequest, MemoryPanelExportResult>
    {
    public:
        /** @brief @p request 를 쓰게 냅니다. */
        void request( MemoryExportRequest request );

    private:
        static void runJob( const TaskArgs& args );
    };

    /**
     * @class MemoryViewModel
     * @brief Memory 창의 ViewModel 입니다. 파일 머리말 참고. 게임(UI) 스레드에서만 부릅니다.
     */
    class MemoryViewModel
    {
    public:
        /** @brief 들고 있는 스냅숏 상한입니다. 넘으면 가장 오래된 것부터 버린다(스냅숏 하나가 에셋 수천 줄이라 늘 쌓아 두지 않는다). */
        static constexpr uint32 kMaxSnapshotCount = 16;

        MemoryViewModel();

        /** @brief 창이 그릴 값을 채웁니다. 끝난 내보내기 잡의 결과를 꺼냅니다. */
        void gather( MemoryViewState& outState );

        /** @brief 이 프로세스의 스냅숏을 찍어 보는 스냅숏으로 둡니다. 설정(`Config/Dev/MemorySnapshot.json`)이 없으면 이유가 상태 줄에 보인다. */
        void captureLocal();
        /** @brief 원격 서버(Multiplayer 창 Inspector 가 붙은 채널)의 스냅숏을 받아 보는 스냅숏으로 둡니다. */
        void captureRemote();
        /** @brief 스냅숏 @p snapshot 을 더하고 보는 스냅숏으로 둡니다(찍기 · 원격 · 테스트가 같은 길). */
        void addSnapshot( MemorySnapshot snapshot );

        /** @brief 보는 스냅숏을 @p index 로 둡니다. 비교의 앞과 같으면 비교를 끈다. */
        void selectSnapshot( int32 index );
        /** @brief 비교의 앞 스냅숏을 @p index 로 둡니다(`kMemoryNoSnapshot` 은 비교 끄기). */
        void setCompareIndex( int32 index );
        /** @brief 보는 스냅숏과 그 바로 앞 스냅숏을 비교합니다(앞이 없으면 아무 일도 없다). */
        void compareWithPrevious();
        /** @brief 시트를 @p sheet 로 바꿉니다. 정렬은 그 시트의 크기 칸 내림차순, 거르기는 처음으로 돌아간다. */
        void selectSheet( MemorySheetKind sheet );
        /** @brief 칸 @p column 으로 정렬합니다. */
        void setSort( uint32 column, bool bDescending );
        /** @brief 이름 거르기(키 칸의 글, 대소문자 무시)를 @p text 로 둡니다. */
        void setNameFilter( string_view text );
        /** @brief 형식 거르기를 @p index(0 은 모두)로 둡니다. */
        void setFilterIndex( uint32 index );

        /** @brief 지금 보는 표(시트 전체 또는 비교 전체 — 바이트는 원값과 MB 칸)를 CSV 로 내보냅니다(워커). */
        void exportTable();
        /** @brief 보는 스냅숏 전체를 한 폴더에 시트별 CSV 로 내보냅니다(워커). */
        void exportSnapshot();

        /** @brief 들고 있는 스냅숏 수입니다(감시 값). */
        uint32 getSnapshotCount() const { return static_cast<uint32>( _listSnapshot.size() ); }
        /** @brief 끝난 내보내기 수(성공 · 실패)입니다 — 시나리오가 비동기 CSV 쓰기를 기다리는 값입니다. */
        uint32 getFinishedExportCount() const { return _finishedExportCount; }
        /** @brief 마지막 `gather` 의 표 줄 수입니다(감시 값). */
        uint32 getVisibleRowCount() const { return _visibleRowCount; }
        /** @brief 비교 중이면 true 입니다. */
        bool isCompareMode() const { return _compareIndex >= 0 && _compareIndex != _currentIndex; }

        /**
         * @brief 스냅숏 @p snapshot 의 시트 @p sheet 를 정렬 · 거르기한 표로 짓습니다(비교가 아닌 표 — 단위 테스트가 부른다).
         * @param filterText 형식 거르기 값(비면 모두)
         */
        static void buildSheetTable( const MemorySnapshot& snapshot, MemorySheetKind sheet, uint32 sortColumn, bool bDescending, string_view nameFilter, string_view filterText,
                                     vector<MemoryTableColumn>& outListColumn, vector<MemoryTableRow>& outListRow );
        /** @brief 비교 @p diff 를 정렬 · 거르기한 표로 짓습니다(키 칸 · Before · After · Delta · Status). */
        static void buildDiffTable( const MemorySheetDiff& diff, uint32 sortColumn, bool bDescending, string_view nameFilter, string_view filterText,
                                    vector<MemoryTableColumn>& outListColumn, vector<MemoryTableRow>& outListRow );
        /** @brief 바이트를 사람용 글(B · KB · MB · GB, 소수 둘째 자리)로 씁니다. */
        static string formatBytes( uint64 bytes );
        /** @brief 부호 있는 바이트(비교의 차이)를 `+` · `-` 를 붙인 글로 씁니다. */
        static string formatSignedBytes( int64 bytes );

    private:
        /** @brief 시트의 형식 거르기 값을 모읍니다(보는 스냅숏의 거르기 칸 글, 이름 순). */
        void collectFilterOptions( vector<string>& outListOption ) const;
        /** @brief 내보내기 폴더를 잡습니다. 못 잡으면 이유를 상태 줄에 두고 빈 글입니다. */
        AbsolutePath reserveExportFolder();

    private:
        vector<shared_ptr<const MemorySnapshot>> _listSnapshot;
        int32                                    _currentIndex;
        int32                                    _compareIndex;
        MemorySheetKind                          _sheet;
        uint32                                   _sortColumn;
        string                                   _nameFilter;
        uint32                                   _filterIndex;
        string                                   _statusLine;
        uint32                                   _visibleRowCount;
        uint32                                   _finishedExportCount;
        MemoryExportJob                          _exportJob;
        uint8                                    _bSortDescending : 1;
        [[maybe_unused]] uint8                   _reserved        : 7;
    };
} // namespace sw::editor
