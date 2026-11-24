/**
 * @file MemorySnapshotExport.h
 * @brief 메모리 스냅숏의 CSV 내보내기입니다 — 표 하나(시트 · 비교)를 CSV 로, 스냅숏 전체를 한 폴더에 시트별 CSV 로. 설정은 `Config/Dev/MemorySnapshot.json` 입니다.
 * @details CSV 는 엑셀이 바로 여는 꼴이다 — UTF-8 BOM, 쉼표, RFC 4180 따옴표(쉼표 · 따옴표 · 줄바꿈이 든 칸은 `"` 로 감싸고 `"` 는 두 번), 줄 끝 CRLF, 첫 줄 머리말.
 *          바이트 칸은 원값(`<칸> (bytes)`)과 사람용 MB 칸(`<칸> (MB)`, 소수 셋째 자리)을 따로 둔다 — 원값으로 정렬 · 합산하고 MB 로 읽는다.
 *          폴더는 `<저장 폴더>/<_exportFolder>/<yyyyMMdd-HHmmss>`(같은 초는 `-2` …)라 덮어쓰지 않는다. 상용 엔진 비교: Godot Video RAM 탭의 CSV 내보내기,
 *          언리얼 CSV 프로파일러(`-csvExitOnCompletion`), 유니티 Memory Profiler 표의 복사.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Path/AbsolutePath.h"

#include "DevTools/DevToolsAPI.h"
#include "DevTools/Profiling/Memory/MemorySnapshot.h"

namespace sw
{
    /**
     * @struct CSVTable
     * @brief CSV 표 하나입니다 — 머리말 한 줄과 줄들(칸 수는 머리말과 같다). 글은 RFC 4180 · UTF-8 BOM · CRLF 로 짓는다.
     */
    struct SW_DEVTOOLS_API CSVTable
    {
        /** @brief UTF-8 BOM 입니다 — 엑셀이 글자 코드를 UTF-8 로 읽는다(없으면 시스템 코드 페이지로 읽어 한글이 깨진다). */
        static constexpr string_view kByteOrderMark = "\xEF\xBB\xBF";

        vector<string>         _listHeader{};
        vector<vector<string>> _listRow{};

        /** @brief 칸 하나를 @p outText 뒤에 씁니다. 쉼표 · 따옴표 · 줄바꿈이 있으면 따옴표로 감싸고 안의 따옴표는 두 번 쓴다. */
        static void appendField( string& outText, string_view field );
        /** @brief 이 표의 CSV 글(BOM · 머리말 · 줄)입니다. */
        string writeText() const;
        /** @brief 이 표를 @p path 에 씁니다(원자 쓰기 — `FileUtil::writeTextFile`). 경로는 UTF-8 이다(한글 폴더 · 이름). */
        [[nodiscard]] bool writeFile( const AbsolutePath& path ) const;
    };

    /** @brief `Config/Dev/MemorySnapshot.json` 의 칸입니다. 칸이 하나라도 없거나 범위 밖이면 읽기가 오류다(기본값이 없다). */
    struct SW_DEVTOOLS_API MemorySnapshotPolicy
    {
        /** @brief 정책 파일(프로젝트 루트 기준)입니다. */
        static constexpr const utf8* kPolicyFile = "Config/Dev/MemorySnapshot.json";

        string _exportFolder{};           ///< 저장 폴더(`ProcessRoots` 의 저장 · 실행 뿌리) 아래 내보내기 폴더 이름(비지 않는다, 구분자 없음)
        uint32 _allocationSiteLimit{ 0 }; ///< 할당 사이트 시트의 줄 상한(1 이상 — 기호 풀기 비용)

        /** @brief 정책 JSON 글을 읽습니다. */
        [[nodiscard]] static expected<MemorySnapshotPolicy> parse( string_view jsonText, string_view sourceName );
        /** @brief 프로젝트의 정책 파일을 읽습니다. */
        [[nodiscard]] static expected<MemorySnapshotPolicy> load();
    };

    /** @brief 내보낸 파일 하나의 결과입니다(로그 · 개발 명령 답 · 패널 알림). */
    struct MemoryExportFile
    {
        string _path{};
        uint32 _rowCount{ 0 };
    };

    /** @brief 내보내기 한 번의 결과입니다. */
    struct MemoryExportResult
    {
        string                   _folder{};
        vector<MemoryExportFile> _listFile{};
    };

    /**
     * @struct MemorySnapshotExport
     * @brief 시트 · 비교를 CSV 표로 바꾸고, 스냅숏을 폴더에 씁니다.
     */
    struct SW_DEVTOOLS_API MemorySnapshotExport
    {
        /** @brief 요약 파일 이름입니다(스냅숏 전체 내보내기의 첫 파일). */
        static constexpr const utf8* kSummaryFileName = "Summary.csv";

        /** @brief 시트 @p kind 를 CSV 표로 바꿉니다(바이트 칸은 원값 + MB 칸). */
        static CSVTable makeSheetTable( const MemorySnapshot& snapshot, MemorySheetKind kind );
        /** @brief 비교 @p diff 를 CSV 표로 바꿉니다(키 칸 · Before · After · Delta 바이트 + MB · Status). */
        static CSVTable makeDiffTable( const MemorySheetDiff& diff );
        /** @brief 스냅숏 머리(이름 · 원천 · GPU · 합 · 시트별 줄 수)를 CSV 표로 바꿉니다. */
        static CSVTable makeSummaryTable( const MemorySnapshot& snapshot );
        /** @brief 바이트를 사람용 MB 글(소수 셋째 자리, 1 MB = 1048576 바이트)로 씁니다. */
        static string formatMegabytes( uint64 bytes );
        /** @brief 부호 있는 바이트(비교의 차이)를 MB 글로 씁니다. */
        static string formatSignedMegabytes( int64 bytes );

        /** @brief 내보내기 폴더의 뿌리입니다 — 혼자 뜬 실행은 `Saved/<_exportFolder>`, 부모가 실행 뿌리를 준 실행은 `<실행 뿌리>/<_exportFolder>`(BugReport 와 같은 규칙). */
        [[nodiscard]] static AbsolutePath getDefaultRoot( const MemorySnapshotPolicy& policy );
        /** @brief @p root 아래 지금 시각의 새 폴더를 잡습니다(같은 초는 `-2` …). 잡지 못하면 빈 글입니다(이유는 로그). */
        [[nodiscard]] static AbsolutePath reserveFolder( const AbsolutePath& root );
        /** @brief @p root 아래 가장 최근(이름 순 마지막) 폴더입니다. 없으면 빈 글입니다. */
        [[nodiscard]] static AbsolutePath findLatestFolder( const AbsolutePath& root );

        /** @brief 스냅숏을 폴더 @p folder(이미 있다)에 Summary + 시트마다 `<시트>.csv` 로 씁니다. 쓴 파일마다 로그 한 줄(`MemoryExport.SheetWritten`). */
        [[nodiscard]] static expected<MemoryExportResult> writeSnapshot( const MemorySnapshot& snapshot, const AbsolutePath& folder );
        /** @brief 표 하나를 @p path 에 쓰고 로그 한 줄(`MemoryExport.SheetWritten`)을 남깁니다. */
        [[nodiscard]] static expected<MemoryExportFile> writeTable( const CSVTable& table, const AbsolutePath& path );
    };
} // namespace sw
