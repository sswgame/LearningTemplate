/**
 * @file PackWriter.h
 * @brief .pack(SWPK) 아카이브를 쓰는 곳 하나(`PackStreamWriter`)입니다. 쿠커(`PackCooker`) · 패치 만들기 · 모드 팩 · 테스트(`PackTestUtil`)가 같이 씁니다.
 * @details 레이아웃은 `PackTypes.h` 의 구조체이고, 리더(`PackReader`)가 같은 구조체를 읽습니다. 쓰는 곳이 하나라 "쓰는 쪽과 읽는 쪽의
 *          오프셋이 어긋난 팩" 이 형식으로 생기지 않습니다(예전에는 파이썬 쿠커 · 테스트 도우미 · 리더가 레이아웃을 따로 들었다).
 *          - **스트림 쓰기.** 항목을 경로 해시 오름차순으로 하나씩 받아(`append`) 블록으로 잘라 압축하고 이어 쓴 뒤, 닫을 때(`close`) 색인 구역과 헤더를 쓴다.
 *            팩 전체를 한 벡터로 들지 않는다 — 예전 `PackWriter` 는 모든 원본과 팩 이미지를 한 번에 들었다(감사 A-10). 순서가 틀린 `append` 는 오류라
 *            부르는 쪽이 경로 해시로 먼저 세운다(`makePathHash`). 같은 해시를 두 번 담아도 오류다 — 리더는 해시 하나에 항목 하나만 든다.
 *          - **병렬 압축.** 받은 항목을 `_writeWindowBytes` 까지 모았다가 블록마다 병렬로 압축 · CRC 하고(`engine::runParallel`, 블록마다 자기 칸에만 쓴다)
 *            쓰기는 받은 순서대로 직렬이다. 그래서 팩 바이트는 직렬로 돌 때와 같다. 최고 메모리는 창 크기의 몇 배와 색인뿐이다(콘텐츠 크기와 무관).
 *          - **내용 해시.** 쓰는 바이트를 `ContentHash::appendBytes` 로 함께 섞어 `close` 가 돌려준다 — 쿠킹 기록이 출력을 다시 읽지 않는다.
 *          - 압축은 팩 단위다. 모든 블록을 같은 코덱으로 넣는다(리더가 헤더의 코덱 하나로 푼다). 빈 항목은 블록이 없고 0 바이트다.
 *          - 바이트 안정성(스팀 청크 차이 받기용): 항목은 경로 해시 순으로 놓이고 블록별로 압축하므로 같은 입력이면 같은 바이트다. 항목 하나가 바뀌어도 그 항목의
 *            페이로드 · FAT · 블록 칸 밖은 그대로다. 단 항목을 더하거나 지우거나 섹터를 넘게 키우면 해시 순으로 뒤에 오는 페이로드가 섹터 단위로 밀린다. 밀림을 없애는 것이
 *            슬롯 배치(`PackLayoutMode::Slot`)다: 앞 빌드의 팩(`_previousPackPath`)이 있으면 안 바뀐 항목이 앞 빌드의 자리를 지키며 커진 항목 · 새 항목은 앞 페이로드의
 *            끝 뒤에 붙는다. 지운 항목의 자리는 비어 남는다(성긴 파일의 구멍). 다 쓴 뒤 이번 빌드의 빈 자리가 `_maxHolePercent` 를 넘으면
 *            쓴 팩의 페이로드를 촘촘히 옮겨 다시 쓴다(`compactCommitted` — 조각 복사, 드문 길).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Content/Pack/PackTypes.h"

namespace sw
{
    class ICompressionCodec;
    class INetSecurityProvider;

    /** @struct PackWriterDesc @brief 팩 하나의 쓰기 설정입니다. `_blockSize` · `_writeWindowBytes` 는 기본값이 없어 비워 두면 `open` 이 오류입니다. */
    struct PackWriterDesc
    {
        PackCompressionType   _compression{ PackCompressionType::None };
        int32                 _compressionLevel{ 0 };                ///< 코덱의 레벨(0 = 코덱 기본값)
        uint32                _dlcAppID{ 0 };                        ///< 0: 본편 · 공용, >0: DLC AppID
        PackLayoutMode        _layoutMode{ PackLayoutMode::Sorted }; ///< 페이로드 배치(파일 머리말의 "슬롯 배치")
        string                _previousPackPath{};                   ///< `Slot` 에서 앞 빌드의 팩 경로. 없는 파일이면 앞 빌드가 없는 첫 배치이고, 열 수 없으면 경고를 남기고 슬롯 없이 배치한다
        uint32                _maxHolePercent{ 25 };                 ///< `Slot` 에서 이번 빌드 페이로드 영역의 빈 자리가 이 비율(%)을 넘으면 촘촘히 다시 놓는다
        string                _mountDomain{};                        ///< 항목 경로 앞에 붙는 리소스 도메인(`engine` · `game/empty` · `dlc/winter`). 비우면 항목이 전체 경로다(패치 팩)
        uint32                _blockSize{ 0 };                       ///< 필수. 압축 전 블록 크기 — `kPackMinBlockSize` ~ `kPackMaxBlockSize` 의 2 의 거듭제곱(PackConfig `_blockSize`)
        uint64                _writeWindowBytes{ 0 };                ///< 필수. 이만큼의 원본을 모아 병렬로 압축한다(PackConfig `_writeWindowBytes`)
        INetSecurityProvider* _pDigestProvider{ nullptr };           ///< 서명 채널(패치 팩)이면 SHA-256 제공자 — 블록 해시 표를 쓰고(`PackFlag::HasBlockHash`) 색인 요약을 결과에 담는다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackWriteStats
     * @brief 팩 하나를 쓰는 데 든 단계별 시간(ns)입니다. 쿠킹 요약 로그가 모아 병렬 효과를 보입니다. 단계는 겹치지 않으므로 합이 곧 쓰기에 든 시간입니다.
     */
    struct PackWriteStats
    {
        int64  _compressWallNanos{ 0 }; ///< 압축 · CRC 구간의 벽시계 시간(병렬이면 가장 느린 스레드가 정한다)
        int64  _compressCPUNanos{ 0 };  ///< 블록마다 잰 압축 · CRC 시간의 합(직렬로 돌았다면 든 시간). 벽시계와의 비가 병렬 효과다
        int64  _layoutNanos{ 0 };       ///< 순서 · 크기 검사, 색인 · 헤더 만들기(나머지)
        int64  _writeNanos{ 0 };        ///< 파일에 쓰고 바꿔 끼운 시간
        uint64 _inputBytes{ 0 };        ///< 담은 원본 바이트의 합
        uint32 _entryCount{ 0 };        ///< 항목 수

        /** @brief @p other 의 시간 · 크기를 더합니다(팩 여럿을 한 줄로 요약할 때). */
        void add( const PackWriteStats& other )
        {
            _compressWallNanos += other._compressWallNanos;
            _compressCPUNanos += other._compressCPUNanos;
            _layoutNanos += other._layoutNanos;
            _writeNanos += other._writeNanos;
            _inputBytes += other._inputBytes;
            _entryCount += other._entryCount;
        }
    };

    /** @struct PackWriteResult @brief `PackStreamWriter::close` 의 결과입니다. */
    struct PackWriteResult
    {
        uint64               _contentHash{ 0 }; ///< 쓴 파일의 `ContentHash::computeBytes` 와 같은 값(다시 읽지 않고 쓰는 동안 셌다)
        uint64               _fileSize{ 0 };    ///< 쓴 파일 크기
        uint64               _indexOffset{ 0 }; ///< 색인 구역(FAT · 블록 표 · 블록 해시 표 · 스트링 풀 · 헤더)의 시작 — 파일 끝까지다
        optional<PackDigest> _indexDigest;      ///< 색인 구역의 SHA-256 — `_pDigestProvider` 를 준 쓰기만. 패치 목록이 이것에 서명한다(`PackPatchManifest::sign`)
        PackWriteStats       _stats;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PackStreamWriter
     * @brief 항목을 경로 해시 순으로 이어 쓰는 팩 쓰기입니다(`open` → `append`… → `close`). 파일 머리말 참고.
     * @details 쓰기는 임시 파일에 하고 `close` 가 성공해야 바꿔 끼운다(`AtomicFileWriter`). 한 번 실패하면 이 쓰기는 끝이다 — 뒤의 `append` · `close` 도 오류이고
     *          대상 파일은 그대로다(소멸이 임시 파일을 지운다).
     */
    class SW_API PackStreamWriter
    {
    public:
        PackStreamWriter();
        ~PackStreamWriter();
        PackStreamWriter( const PackStreamWriter& )            = delete;
        PackStreamWriter& operator=( const PackStreamWriter& ) = delete;

        /** @brief 팩 안 경로의 해시입니다(구분자를 '/' 로 맞춘 뒤 대소문자 무시 FNV-1a 64). `append` 에 앞서 이 값으로 항목을 세운다. */
        static uint64 makePathHash( string_view relativePath );

        /**
         * @brief @p packPath 에 쓸 팩을 엽니다(부모 폴더를 만든다).
         * @return 블록 크기 · 창 크기가 없거나 범위 밖이거나, 코덱이 없거나, 파일을 열 수 없으면 오류입니다.
         */
        [[nodiscard]] expected<void> open( string_view packPath, const PackWriterDesc& desc );

        /**
         * @brief 팩 안 경로 @p relativePath 에 @p bytes 를 @p storage 방식으로 담습니다. 창이 차면 그 자리에서 압축해 씁니다.
         * @param storage 이미 압축된 형식은 `Stored`(쿠커는 PackConfig `_listStoredExtension` 으로 정한다)
         * @return 경로 해시가 앞 항목보다 크지 않거나(순서 · 중복), 항목이 리더의 크기 한도를 넘거나, 압축 · 쓰기가 실패하면 오류입니다.
         */
        [[nodiscard]] expected<void> append( string_view relativePath, PackEntryStorage storage, vector<uint8> bytes );

        /** @brief 남은 항목과 색인 구역 · 헤더를 쓰고 대상 파일과 바꿔 끼웁니다. 성공이든 실패든 이 쓰기는 닫힌다. */
        [[nodiscard]] expected<PackWriteResult> close();

        /** @brief 지금까지 받은 항목 수입니다. */
        uint32 getEntryCount() const { return _entryCount; }

    private:
        /** @brief 쓰기의 단계입니다. */
        enum class State : uint8
        {
            Closed, ///< 열지 않았거나 닫았다
            Open,   ///< 받는 중
            Failed, ///< 한 번 실패했다 — 뒤의 호출은 모두 오류다
        };
        /** @brief 창에 모아 둔 항목 하나입니다. */
        struct PendingEntry
        {
            string           _relativePath;
            uint64           _pathHash{ 0 };
            vector<uint8>    _bytes;
            PackEntryStorage _storage{ PackEntryStorage::Blocks };
        };
        /** @brief 앞 빌드 팩에서 항목 하나가 차지한 자리입니다. */
        struct PreviousSlot
        {
            uint64 _offset{ 0 };
            uint64 _capacity{ 0 }; ///< 이 자리에 담을 수 있는 최대 바이트(다음 페이로드 오프셋까지)
        };
        /** @brief 블록 하나의 압축 결과 칸입니다(정의는 .cpp). */
        struct BlockSlot;
        /** @brief 블록들을 압축하는 병렬 본문입니다(정의는 .cpp). */
        struct CompressJob;

        /** @brief 실패를 적어 두고 그 오류를 돌려줍니다 — 뒤의 호출이 모두 이 쓰기를 끝난 것으로 본다. */
        [[nodiscard]] unexpected<Error> fail( Error error );
        /** @brief `Slot` 이고 앞 빌드의 팩이 있으면 그 항목 자리를 읽고 페이로드 구역을 0 으로 미리 채웁니다. */
        [[nodiscard]] expected<void> loadPreviousSlots();
        /** @brief 창의 항목을 블록으로 잘라 병렬로 압축하고 받은 순서대로 씁니다. */
        [[nodiscard]] expected<void> flushPending();
        /** @brief @p offset 에 @p size 바이트를 씁니다. 끝이면 덧붙이고 내용 해시에 섞는다(슬롯 배치의 앞 자리는 덮어쓴다). */
        [[nodiscard]] expected<void> writeBytes( uint64 offset, const uint8* pData, size_t size );
        /** @brief 파일 끝을 섹터 경계까지 0 으로 채웁니다. */
        [[nodiscard]] expected<void> padToSector();
        /** @brief 남은 바이트를 덧붙인 뒤 색인 구역 · 헤더를 쓰고 바꿔 끼웁니다(`close` 와 `compactCommitted` 가 같이 쓴다). */
        [[nodiscard]] expected<PackWriteResult> writeIndexAndCommit();
        /** @brief 슬롯 배치로 바꿔 끼운 팩의 빈 자리가 한도를 넘었을 때, 그 팩의 페이로드를 FAT 순서로 촘촘히 옮겨 같은 자리에 다시 씁니다. */
        [[nodiscard]] expected<PackWriteResult> compactCommitted();

    private:
        PackWriterDesc                      _desc;
        AtomicFileWriter                    _file;
        string                              _packPath;
        ICompressionCodec*                  _pCodec;
        vector<PendingEntry>                _listPending;
        uint64                              _pendingBytes;
        vector<PackFileEntryOnDisk>         _listDiskEntry;
        vector<PackBlockEntryOnDisk>        _listBlock;
        vector<PackDigest>                  _listBlockDigest; ///< `_pDigestProvider` 가 있을 때만 — 블록마다 저장 바이트의 SHA-256
        vector<uint8>                       _stringPoolBytes;
        unordered_map<uint64, PreviousSlot> _mapPreviousSlot;  ///< 비어 있으면 모든 쓰기가 덧붙이기다
        uint64                              _payloadEnd;       ///< 페이로드 구역의 끝(다음 덧붙이기 자리 앞)
        uint64                              _usedPayloadBytes; ///< 이번 빌드 항목이 차지한 페이로드(섹터 정렬) — 슬롯 배치의 빈 자리 판정
        uint64                              _contentHash;      ///< 덧붙인 바이트의 내용 해시 상태
        uint64                              _lastPathHash;
        uint32                              _entryCount;
        PackWriteStats                      _stats;
        Stopwatch                           _openWatch;
        State                               _state;
    };
} // namespace sw
