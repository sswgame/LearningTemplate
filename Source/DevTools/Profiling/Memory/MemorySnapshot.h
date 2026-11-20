/**
 * @file MemorySnapshot.h
 * @brief 한 시점의 메모리 스냅숏(일반 표 여섯)과 두 스냅숏 비교, 원격 전송 글입니다. 개발 도구에 있다 — 배포 호스트는 DevTools 를 링크하지 않는다.
 * @details 시트(Tags · AllocationSites · Assets · GPUResources · Types · Caches)마다 칸 서술이 표 하나(`getMemorySheetSchema`)에 있고, 줄은 칸마다 글 또는 숫자다.
 *          비교 · CSV · 에디터 표 · 원격 글이 시트별 코드를 갖지 않는다 — 시트를 더하면 서술 한 줄과 수집 함수 하나만 는다.
 *          상용 엔진 비교: 유니티 Memory Profiler 의 스냅숏(표 여럿 · 두 스냅숏 비교), 언리얼 `memreport -full` 의 표 묶음(`obj list` · `ListTextures` · `rhi.DumpMemory`).
 *          설계는 `docs/DesignBacklog.md` "메모리 사용량 디버그 · 내보내기".
 */
#pragma once
#include "Core/Container/array.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @brief 스냅숏의 시트입니다. 순서가 파일 · 탭 순서입니다. */
    enum class MemorySheetKind : uint8
    {
        Tags,            ///< CPU 메모리 태그(지금 · 최고 · 예산 · 할당 수)
        AllocationSites, ///< 할당 사이트 상위(콜스택 — 세부 추적이 켜져 있을 때)
        Assets,          ///< 캐시가 쥔 에셋(종류 · 경로 · CPU · GPU · 쥔 쪽 수)
        GPUResources,    ///< GPU 자원(텍스처 · 버퍼 · 렌더 타깃 — 이름 · 포맷 · 크기 · 밉 · 바이트)
        Types,           ///< 오브젝트 · 컴포넌트 형식별 수 · 바이트(리플렉션 형식 이름)
        Caches,          ///< 캐시 계수(항목 · 적중 · 실패 · 비우기)
        MaxKinds
    };

    /** @brief 시트 수입니다. */
    inline constexpr uint32 kMemorySheetCount = static_cast<uint32>( MemorySheetKind::MaxKinds );

    /** @brief 칸의 값 종류입니다. */
    enum class MemoryColumnKind : uint8
    {
        Text,  ///< 글(이름 · 경로 · 포맷)
        Bytes, ///< 바이트(CSV 는 원값과 MB 칸 둘)
        Count  ///< 수
    };

    /** @brief 칸의 쓰임입니다. 키 칸들의 글이 한 줄의 정체다(비교가 두 스냅숏의 줄을 맞춘다). */
    enum class MemoryColumnRole : uint8
    {
        Value, ///< 값
        Key    ///< 줄의 정체(글 칸만)
    };

    /** @brief 칸 하나의 서술입니다. */
    struct MemoryColumn
    {
        const utf8*      _pName;
        MemoryColumnKind _kind;
        MemoryColumnRole _role;
    };

    /** @brief "그런 칸 없음" 값입니다(`MemorySheetSchema::_filterColumn`). */
    inline constexpr uint32 kMemoryNoColumn = 0xFFFFFFFFu;

    /** @brief 시트 하나의 서술입니다(`getMemorySheetSchema`). */
    struct MemorySheetSchema
    {
        const utf8*         _pName;        ///< 시트 이름(CSV 파일 이름 · 탭 이름)
        const MemoryColumn* _pColumn;      ///< 칸 서술(정적 표)
        uint32              _columnCount;  ///< 칸 수
        uint32              _sizeColumn;   ///< 합 · 비교 · 기본 정렬의 칸(바이트나 수)
        uint32              _filterColumn; ///< 형식 거르기의 칸(글). 없으면 `kMemoryNoColumn`
    };

    /** @brief 시트 @p kind 의 서술입니다. */
    SW_DEVTOOLS_API const MemorySheetSchema& getMemorySheetSchema( MemorySheetKind kind );
    /** @brief 이름이 @p name 인 시트를 @p outKind 에 담습니다(대소문자 구분). 없으면 false 입니다. */
    [[nodiscard]] SW_DEVTOOLS_API bool findMemorySheetKind( string_view name, MemorySheetKind& outKind );

    /** @brief 칸 하나의 값입니다. 글 칸은 `_text`, 숫자 칸은 `_value` 만 쓴다. */
    struct MemoryCell
    {
        string _text{};
        uint64 _value{ 0 };
    };

    /** @brief 줄 하나입니다. 칸 수는 시트 서술의 칸 수와 같다. */
    struct SW_DEVTOOLS_API MemoryRow
    {
        vector<MemoryCell> _listCell{};

        /** @brief 키 칸의 글을 서술 순서로 @p outListKeyText 에 담습니다(비운 뒤). 비교가 두 스냅숏의 줄을 이것으로 맞춘다. */
        void collectKeyText( const MemorySheetSchema& schema, vector<string>& outListKeyText ) const;
    };

    /** @brief 시트 하나의 줄들입니다. */
    struct MemorySheet
    {
        vector<MemoryRow> _listRow{};
    };

    /**
     * @struct MemorySnapshot
     * @brief 한 시점의 메모리 스냅숏입니다. 값이라 찍은 뒤에는 바뀌지 않는다.
     */
    struct SW_DEVTOOLS_API MemorySnapshot
    {
        string                                _label{};           ///< 사람용 이름(찍은 시각 `yyyyMMdd-HHmmss` · 원천)
        string                                _source{};          ///< 원천 — `local` 또는 원격 주소
        string                                _gpuDescription{};  ///< GPU 백엔드 이름과 바이트 기준(`allocation` · `logical`). GPU 가 없으면 비어 있다
        uint64                                _cpuLiveBytes{ 0 }; ///< 태그 표의 지금 바이트 합
        uint64                                _gpuLiveBytes{ 0 }; ///< GPU 자원 표의 바이트 합
        array<MemorySheet, kMemorySheetCount> _arrSheet{};

        MemorySheet&       getSheet( MemorySheetKind kind ) { return _arrSheet[static_cast<uint32>( kind )]; }
        const MemorySheet& getSheet( MemorySheetKind kind ) const { return _arrSheet[static_cast<uint32>( kind )]; }

        /**
         * @brief 원격 전송 글로 씁니다(개발 명령 `memory.snapshot` 의 답). 줄마다 탭으로 가른 칸이고 글 칸의 탭 · 줄바꿈 · 역슬래시는 `\t` · `\n` · `\\` 로 쓴다.
         * @details 첫 줄 `swmem 1`, 머리 줄(`label` · `source` · `gpu` · `cpu` · `gpubytes`), 시트마다 `sheet <이름> <줄 수>` 뒤에 줄들. CSV 가 아닌 까닭: 받는 쪽이 따옴표 규칙 없이
         *          줄 단위로 읽고, 숫자를 MB 칸 없이 원값 하나로 나른다.
         */
        string writeText() const;
        /** @brief `writeText` 의 글을 읽습니다. 머리 · 시트 이름 · 칸 수 · 숫자가 틀리면 false 이고 @p outSnapshot 은 정해지지 않는다. */
        [[nodiscard]] static bool readText( string_view text, MemorySnapshot& outSnapshot );
        /** @brief 지금 지역 시각의 이름(`yyyyMMdd-HHmmss`)입니다 — 스냅숏 이름 · 내보내기 폴더 이름이 같은 형식이다(BugReport 폴더와 같다). */
        static string makeTimeLabel();
    };
} // namespace sw

namespace sw
{
    /** @brief 비교 줄의 상태입니다. */
    enum class MemoryDiffStatus : uint8
    {
        New,    ///< 뒤 스냅숏에만 있다
        Grown,  ///< 크기 칸이 늘었다
        Shrunk, ///< 크기 칸이 줄었다
        Gone,   ///< 앞 스냅숏에만 있다
        Same    ///< 같다
    };

    /** @brief 상태 이름입니다(표 · CSV 의 글). */
    SW_DEVTOOLS_API const utf8* getMemoryDiffStatusName( MemoryDiffStatus status );

    /** @brief 비교 줄 하나입니다 — 키 칸의 글과 크기 칸의 앞 · 뒤 값. 같은 키가 한 시트에 여럿이면 값을 더해 한 줄이다. */
    struct MemoryDiffRow
    {
        vector<string>   _listKeyText{}; ///< 키 칸의 글(서술 순서)
        uint64           _before{ 0 };
        uint64           _after{ 0 };
        int64            _delta{ 0 }; ///< 뒤 − 앞
        MemoryDiffStatus _status{ MemoryDiffStatus::Same };
    };

    /**
     * @struct MemorySheetDiff
     * @brief 시트 하나의 비교입니다 — 두 스냅숏의 줄을 키로 맞춰 크기 칸의 차이를 낸다(유니티 Memory Profiler 의 Compare 모드). 줄은 늘어난 것부터(차이 내림차순, 같으면 키 글 순)입니다.
     */
    struct SW_DEVTOOLS_API MemorySheetDiff
    {
        MemorySheetKind       _kind{ MemorySheetKind::Tags };
        vector<MemoryDiffRow> _listRow{};
        int64                 _totalDelta{ 0 }; ///< 크기 칸 합의 차이

        /** @brief 시트 @p kind 를 앞 @p before · 뒤 @p after 로 비교합니다. */
        static MemorySheetDiff compare( const MemorySnapshot& before, const MemorySnapshot& after, MemorySheetKind kind );
    };
} // namespace sw
