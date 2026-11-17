#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/Pack/PackTypes.h"

namespace sw
{
    /**
     * @class PackReader
     * @brief .pack(SWPK) 바이너리 아카이브 하나를 열고 64비트 경로 해시로 항목을 읽는 VFS 리더입니다.
     * @details 파일은 `AsyncFileHandle`(위치 지정 읽기)로 엽니다. 공유 파일 위치가 없어 여러 스레드의 `readFile` 이 잠금 없이 동시에 읽고,
     *          잠금은 색인을 찾는 동안만 쥡니다(압축 해제 · CRC 는 잠금 밖). 같은 핸들로 `readFileAsync` 가 비동기 IO 에 구간 읽기를 겁니다.
     *          **색인은 디스크 그대로다.** FAT(경로 해시 오름차순 — 아니면 열지 않는다)을 정렬 배열로 들고 이진 탐색하며, 경로는 스트링 풀을 가리키고
     *          폴더 나열은 (폴더 해시, 항목 번호) 정렬 배열이다. 예전에는 항목마다 경로 사본을 지은 해시 맵 둘을 들었다(감사 A-12) — 항목당 48 B + 풀이다.
     *          범위 읽기(`readRange`)는 걸친 블록만 읽어 푼다(밉 범위 읽기 R-6 의 자리).
     */
    class SW_API PackReader
    {
    public:
        PackReader();
        ~PackReader();

        PackReader( const PackReader& )            = delete;
        PackReader& operator=( const PackReader& ) = delete;

        PackReader( PackReader&& other ) noexcept;
        PackReader& operator=( PackReader&& other ) noexcept;

        /**
         * @brief .pack 파일을 열고 헤더와 FAT · 블록 표 · 스트링 풀을 메모리에 로드합니다.
         * @param packFilePath .pack 파일의 실제 경로
         * @return 유효한 SWPK 아카이브이고 인덱스를 읽었으면 true 입니다. FAT 이 경로 해시 오름차순이 아니면(같은 해시 둘 포함) 거절합니다.
         */
        [[nodiscard]] bool open( string_view packFilePath );

        /**
         * @brief 열려 있는 팩 파일을 닫고 인덱스 메모리를 해제합니다. 진행 중인 읽기는 제 파일 핸들 사본으로 끝까지 읽습니다.
         */
        void close();

        /** @brief 팩 파일이 열려 있는지 반환합니다. */
        bool isOpen() const;

        /** @brief 64비트 경로 해시로 파일이 있는지 확인합니다(이진 탐색). */
        bool hasFile( uint64 pathHash ) const;

        /** @brief 가상 상대 경로로 파일이 있는지 확인합니다. */
        bool hasFile( string_view relativePath ) const;

        /** @brief 파일 항목의 메타데이터를 찾습니다(경로는 스트링 풀에서 복사한다). */
        bool getFileEntry( uint64 pathHash, PackFileEntry& outEntry ) const;
        bool getFileEntry( string_view relativePath, PackFileEntry& outEntry ) const;

        /**
         * @brief @p folder 바로 아래 항목(하위 폴더 안은 빼고)의 팩 안 경로를 @p outListPath 뒤에 덧붙입니다. 폴더 비교는 대소문자를 가리지 않습니다.
         * @param folder 팩 안 폴더(구분자 '/', 끝 '/' 없음). 빈 값이면 팩 맨 위 항목입니다.
         * @details 항목 경로 목록(스트링 풀)은 `open` 이 필수로 읽고 해시와 맞춰 두었으므로 나열은 실패하지 않습니다. 순서는 정하지 않습니다.
         */
        void collectFolderEntries( string_view folder, vector<string>& outListPath ) const;

        /** @brief 팩에 든 모든 항목의 팩 안 경로(하위 폴더 포함)를 @p outListPath 뒤에 덧붙입니다. 순서는 정하지 않습니다(패치 목록 셈이 정렬한다). */
        void collectAllEntryPaths( vector<string>& outListPath ) const;

        /**
         * @brief 팩 안의 파일 데이터를 읽어 블록마다 압축을 풀고 CRC32 를 검증해 반환합니다.
         * @param pathHash 64비트 경로 해시
         * @param outBytes 압축을 푼 원본 데이터 버퍼
         * @return 파일을 읽고 CRC32 무결성 검증에 성공하면 true 입니다.
         */
        [[nodiscard]] bool readFile( uint64 pathHash, vector<uint8>& outBytes ) const;

        /** @brief 가상 상대 경로로 파일을 읽습니다. */
        [[nodiscard]] bool readFile( string_view relativePath, vector<uint8>& outBytes ) const;

        /** @brief 가상 상대 경로로 텍스트 파일(UTF-8)을 읽습니다. */
        [[nodiscard]] bool readTextFile( string_view relativePath, string& outText ) const;

        /**
         * @brief 항목의 원본 [@p offset, @p offset + @p size) 만 읽습니다. 걸친 블록만 디스크에서 읽어 풀고 CRC 를 맞춥니다.
         * @return 항목이 없거나, 범위가 항목 밖이거나(오류 로그), 읽기 · 풀기 · CRC 가 실패하면 false 입니다. 결과는 같은 범위를 통째 읽기에서 자른 것과 같다.
         */
        [[nodiscard]] bool readRange( uint64 pathHash, uint64 offset, uint32 size, vector<uint8>& outBytes ) const;

        /**
         * @brief 항목 하나를 @p io 로 비동기로 읽습니다. 압축 해제 · CRC 는 IO 완료를 받은 쪽(엔진은 태스크 워커)에서 하고 @p onComplete 를 부릅니다.
         * @details 리더를 닫거나 지워도 걸린 읽기는 끝납니다(파일 핸들과 항목 정보를 요청이 들고 간다). 항목이 없으면 아무것도 걸지 않고
         *          유효하지 않은 핸들을 돌려줍니다 — 그때 @p onComplete 는 불리지 않습니다.
         * @return 건 읽기의 핸들(취소 · 기다리기). 항목이 없으면 `isValid() == false` 입니다.
         */
        AsyncReadHandle readFileAsync( AsyncFileIO& io, uint64 pathHash, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const;

        /** @brief `readRange` 의 비동기 꼴입니다. 항목이 없거나 범위가 항목 밖이면 아무것도 걸지 않고 유효하지 않은 핸들입니다(@p onComplete 는 불리지 않는다). */
        AsyncReadHandle readRangeAsync( AsyncFileIO& io, uint64 pathHash, uint64 offset, uint32 size, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const;

        /** @brief 팩 헤더의 사본입니다(잠금 아래에서 복사한다 — 참조는 `close` 와 겹친다). */
        PackHeader getHeader() const;

        /** @brief DLC 식별자를 반환합니다(0 = 본편, >0 = DLC AppID). */
        uint32 getDLCAppID() const;

        /** @brief 팩에 든 파일 총 개수를 반환합니다. */
        uint32 getFileCount() const;

        /** @brief 연 .pack 파일의 실제 경로의 사본입니다(잠금 아래에서 복사한다). */
        string getPackPath() const;

        /** @brief 팩이 적은 마운트 도메인의 사본입니다(`engine` · `game/empty` · `dlc/winter`). 패치 팩은 빈 글이다(항목이 전체 경로). */
        string getMountDomain() const;

    private:
        /** @brief `_listEntry` 안 번호입니다. */
        using EntryIndex = uint32;

        /** @brief 폴더 나열 색인 한 줄 — 폴더(마지막 '/' 앞, 대소문자 무시) 경로 해시와 그 바로 아래 항목입니다. (폴더 해시, 항목 번호) 순으로 세운다. */
        struct FolderEntry
        {
            uint64     _folderHash{ 0 };
            EntryIndex _entryIndex{ 0 };
        };

        /**
         * @brief 항목 하나의 원본 범위를 읽고 풀 때 필요한 것 — 잠금 안에서 찍어 두고 잠금 밖에서 씁니다. 걸친 블록 줄을 사본으로 든다(비동기 완료가 리더보다 오래 산다).
         */
        struct PackReadPlan
        {
            uint64                       _storedOffset{ 0 };     ///< 첫 블록의 저장 바이트가 파일에서 시작하는 자리
            uint32                       _storedBegin{ 0 };      ///< 첫 블록의 저장 바이트가 항목 안에서 시작하는 자리(앞 블록의 누적 끝)
            uint32                       _firstBlockIndex{ 0 };  ///< 항목 안 첫 블록 번호
            uint32                       _uncompressedSize{ 0 }; ///< 항목의 원본 크기(마지막 블록의 크기를 정한다)
            uint32                       _blockSize{ 0 };
            uint32                       _sliceOffset{ 0 }; ///< 푼 블록들 안에서 돌려줄 범위의 시작
            uint32                       _sliceSize{ 0 };   ///< 돌려줄 크기
            PackCompressionType          _compression{ PackCompressionType::None };
            bool                         _bVerifyCRC{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            vector<PackBlockEntryOnDisk> _listBlock;           ///< 걸친 블록 줄

            /** @brief 디스크에서 읽을 바이트 수입니다. */
            uint32 getStoredSize() const { return _listBlock.empty() ? 0 : _listBlock.back()._compressedEnd - _storedBegin; }
        };

        /** @brief `makeReadPlan` 의 결과입니다(잠금 안에서는 로그를 쓰지 않는다 — 범위 밖은 부른 쪽이 알린다). */
        enum class ReadPlanResult : uint8
        {
            Ready,
            Missing,      ///< 항목이 없거나 닫혔다
            OutsideEntry, ///< 범위가 항목 밖이다
        };

        /** @brief `makeReadPlan` 의 크기로 주면 @p offset 부터 항목 끝까지입니다. */
        static constexpr uint64 kToEntryEnd = ~uint64{ 0 };

        /** @brief @p pathHash 를 이진 탐색합니다. 없으면 nullopt 입니다. */
        optional<EntryIndex> findEntryIndex( uint64 pathHash ) const SW_REQUIRES_LOCK( _fileMutex );
        /** @brief 항목 @p entryIndex 의 경로(스트링 풀 안)입니다. 열 때 풀 안의 종결자를 확인했다. */
        string_view getEntryPath( EntryIndex entryIndex ) const SW_REQUIRES_LOCK( _fileMutex );
        /** @brief @p pathHash 항목의 원본 [@p offset, @p offset + @p size) 읽기 계획과 파일 핸들 사본을 잠금 안에서 찍습니다. */
        ReadPlanResult makeReadPlan( uint64 pathHash, uint64 offset, uint64 size, PackReadPlan& outPlan, AsyncFileHandle& outFile ) const;
        /** @brief 계획대로 동기로 읽어 풉니다(`readFile` · `readRange` 의 본문). */
        [[nodiscard]] bool readPlanned( uint64 pathHash, uint64 offset, uint64 size, vector<uint8>& outBytes ) const;
        /** @brief 계획대로 비동기 읽기를 겁니다(`readFileAsync` · `readRangeAsync` 의 본문). */
        AsyncReadHandle readPlannedAsync( AsyncFileIO& io, uint64 pathHash, uint64 offset, uint64 size, AsyncIOPriority priority, const AssetReadCompleteDelegate& onComplete ) const;
        /**
         * @brief 디스크에서 읽은 걸친 블록의 저장 바이트(@p storedBytes)를 블록마다 풀고 CRC 를 맞춘 뒤 계획의 범위를 @p outBytes 에 담습니다. 동기 · 비동기 읽기가 같은 길을 씁니다.
         * @param packPath 실패 로그에 적을 팩 경로
         */
        static bool decodeStoredBytes( const PackReadPlan& plan, string_view packPath, const vector<uint8>& storedBytes, vector<uint8>& outBytes );

        /**
         * @brief 헤더가 말하는 인덱스 · 블록 표 · 스트링 풀 구역이 **실제 파일 안에** 있는지 확인합니다.
         * @details 헤더의 수는 파일에서 온 값입니다. 그것을 그대로 믿고 `resize` 하면 손상된 팩
         *          하나가 거대한 할당 요청이 됩니다. `_indexSize` 와 `_fileCount` 가 같은 것을
         *          두 번 말하는 것도 여기서 맞춰 봅니다.
         * @param fileSize 실제 파일 크기입니다(헤더는 마지막 `sizeof( PackHeader )` 바이트다).
         */
        bool validateHeaderGeometry( uint64 fileSize ) const SW_REQUIRES_LOCK( _fileMutex );
        /** @brief FAT 항목 하나가 페이로드 구역 안을 가리키는지, 크기가 다룰 만한지, 블록 줄이 앞 항목에 이어지고 그 누적 끝이 항목 크기와 맞는지 봅니다. */
        bool               validateFileEntry( const PackFileEntryOnDisk& diskEntry, uint64 payloadEnd, uint32 expectedFirstBlock ) const SW_REQUIRES_LOCK( _fileMutex );
        [[nodiscard]] bool loadIndexTable() SW_REQUIRES_LOCK( _fileMutex );
        /**
         * @brief 항목을 폴더별로 세운 `_listFolderEntry` 를 짓습니다(`loadIndexTable` 이 항목을 다 읽은 뒤 한 번 부릅니다).
         * @details 폴더 나열(`collectFolderEntries`)이 팩 전체를 잠금 안에서 훑지 않고 그 폴더의 항목만 읽게 합니다.
         */
        void populateFolderIndex() SW_REQUIRES_LOCK( _fileMutex );
        /**
         * @brief @p other 의 파일 · 헤더 · 인덱스를 넘겨받고 @p other 의 파일 핸들을 비웁니다. 두 쪽의 `_fileMutex` 를 잡은 채로 부릅니다.
         * @details 이동 생성자와 이동 대입이 함께 씁니다. 멤버를 더하면 여기 한 곳만 고칩니다.
         */
        void takeFromLocked( PackReader& other ) SW_REQUIRES_LOCK( _fileMutex, other._fileMutex );
        /** @brief 파일을 닫고 인덱스를 비웁니다(`close` · `open` · 이동 대입의 잠금 안 부분). */
        void closeLocked() SW_REQUIRES_LOCK( _fileMutex );

    private:
        mutable mutex                           _fileMutex;
        AsyncFileHandle _file                   SW_GUARDED_BY( _fileMutex ); ///< 위치 지정 읽기 핸들(사본을 든 읽기는 닫힌 뒤에도 끝까지 읽는다)
        string _packFilePath                    SW_GUARDED_BY( _fileMutex );
        PackHeader _header                      SW_GUARDED_BY( _fileMutex );
        vector<PackFileEntryOnDisk> _listEntry  SW_GUARDED_BY( _fileMutex ); ///< 디스크 FAT 그대로(경로 해시 오름차순 — 열 때 확인했다)
        vector<PackBlockEntryOnDisk> _listBlock SW_GUARDED_BY( _fileMutex ); ///< 디스크 블록 표 그대로
        vector<FolderEntry> _listFolderEntry    SW_GUARDED_BY( _fileMutex ); ///< (폴더 해시, 항목 번호) 오름차순. `populateFolderIndex` 가 짓고 읽기만 한다
        string _mountDomain                     SW_GUARDED_BY( _fileMutex ); ///< 스트링 풀 맨 앞의 마운트 도메인
        vector<utf8> _stringPoolBytes           SW_GUARDED_BY( _fileMutex );
    };

} // namespace sw
