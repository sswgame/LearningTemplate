#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/AsyncFileIO.h"

#include "Engine/Resource/Pack/ResourcePakTypes.h"
#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    /**
     * @class ResourcePakReader
     * @brief .pack(SWPK) 바이너리 아카이브 하나를 열고 64비트 해시로 O(1) 파일 읽기를 하는 VFS 리더입니다.
     * @details 파일은 `AsyncFileHandle`(위치 지정 읽기)로 엽니다. 공유 파일 위치가 없어 여러 스레드의 `readFile` 이 잠금 없이 동시에 읽고,
     *          잠금은 색인을 찾는 동안만 쥡니다(압축 해제 · CRC 는 잠금 밖). 같은 핸들로 `readFileAsync` 가 비동기 IO 에 구간 읽기를 겁니다.
     */
    class SW_API ResourcePakReader
    {
    public:
        ResourcePakReader();
        ~ResourcePakReader();

        ResourcePakReader( const ResourcePakReader& )            = delete;
        ResourcePakReader& operator=( const ResourcePakReader& ) = delete;

        ResourcePakReader( ResourcePakReader&& other ) noexcept;
        ResourcePakReader& operator=( ResourcePakReader&& other ) noexcept;

        /**
         * @brief .pack 파일을 열고 헤더와 FAT 인덱스 테이블을 메모리에 로드합니다.
         * @param packFilePath .pack 파일의 실제 경로
         * @return 유효한 SWPK 아카이브이고 인덱스를 읽었으면 true 입니다.
         */
        [[nodiscard]] bool open( string_view packFilePath );

        /**
         * @brief 열려 있는 팩 파일을 닫고 인덱스 메모리를 해제합니다. 진행 중인 읽기는 제 파일 핸들 사본으로 끝까지 읽습니다.
         */
        void close();

        /** @brief 팩 파일이 열려 있는지 반환합니다. */
        bool isOpen() const;

        /** @brief 64비트 경로 해시로 파일이 있는지 확인합니다(O(1)). */
        bool hasFile( uint64 pathHash ) const;

        /** @brief 가상 상대 경로로 파일이 있는지 확인합니다(O(1)). */
        bool hasFile( string_view relativePath ) const;

        /** @brief 파일 항목의 메타데이터를 찾습니다. */
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
         * @brief 팩 안의 파일 데이터를 읽어 CRC32 를 검증하고 압축을 풀어 반환합니다.
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
         * @brief 항목 하나를 @p io 로 비동기로 읽습니다. 압축 해제 · CRC 는 IO 완료를 받은 쪽(엔진은 태스크 워커)에서 하고 @p onComplete 를 부릅니다.
         * @details 리더를 닫거나 지워도 걸린 읽기는 끝납니다(파일 핸들과 항목 정보를 요청이 들고 간다). 항목이 없으면 아무것도 걸지 않고
         *          유효하지 않은 핸들을 돌려줍니다 — 그때 @p onComplete 는 불리지 않습니다.
         * @return 건 읽기의 핸들(취소 · 기다리기). 항목이 없으면 `isValid() == false` 입니다.
         */
        AsyncReadHandle readFileAsync( AsyncFileIO& io, uint64 pathHash, AsyncIOPriority priority, const ResourceReadCompleteDelegate& onComplete ) const;

        /** @brief 팩 헤더를 반환합니다. */
        const PackHeader& getHeader() const;

        /** @brief DLC 식별자를 반환합니다(0 = 본편, >0 = DLC AppID). */
        uint32 getDlcAppID() const;

        /** @brief 팩에 든 파일 총 개수를 반환합니다. */
        uint32 getFileCount() const;

        /** @brief 연 .pack 파일의 실제 경로를 반환합니다. */
        const string& getPackPath() const;

    private:
        /** @brief 항목 하나를 읽고 풀 때 필요한 것 — 잠금 안에서 찍어 두고 잠금 밖에서 씁니다. */
        struct PackReadPlan
        {
            uint64              _dataOffset{ 0 };
            uint32              _compressedSize{ 0 };
            uint32              _uncompressedSize{ 0 };
            uint32              _crc32{ 0 };
            PackCompressionType _compression{ PackCompressionType::None };
            bool                _bVerifyCrc{ false };

            /** @brief 디스크에서 읽을 바이트 수입니다(비압축이면 원본 크기). */
            uint32 getStoredSize() const { return _compression == PackCompressionType::None ? _uncompressedSize : _compressedSize; }
        };

        /** @brief @p pathHash 항목의 읽기 계획과 파일 핸들 사본을 잠금 안에서 찍습니다. 없거나 닫혔으면 false 입니다. */
        bool makeReadPlan( uint64 pathHash, PackReadPlan& outPlan, AsyncFileHandle& outFile ) const;
        /**
         * @brief 디스크에서 읽은 바이트(@p storedBytes)를 풀고 CRC 를 맞춰 @p outBytes 에 담습니다. 동기 · 비동기 읽기가 같은 길을 씁니다.
         * @param packPath 실패 로그에 적을 팩 경로
         */
        static bool decodeStoredBytes( const PackReadPlan& plan, string_view packPath, vector<uint8>& storedBytes, vector<uint8>& outBytes );

        /**
         * @brief 헤더가 말하는 인덱스 · 스트링 풀 구역이 **실제 파일 안에** 있는지 확인합니다.
         * @details 헤더의 수는 파일에서 온 값입니다. 그것을 그대로 믿고 `resize` 하면 손상된 팩
         *          하나가 거대한 할당 요청이 됩니다. `_indexSize` 와 `_fileCount` 가 같은 것을
         *          두 번 말하는 것도 여기서 맞춰 봅니다.
         * @param outFileSize 실제 파일 크기입니다. 항목 검사(`validateFileEntry`)가 같은 값을 씁니다.
         */
        bool validateHeaderGeometry( uint64& outFileSize ) const;
        /** @brief FAT 항목 하나가 파일 안을 가리키는지, 크기가 다룰 만한지 봅니다. */
        bool               validateFileEntry( const PackFileEntryOnDisk& diskEntry, uint64 fileSize ) const;
        [[nodiscard]] bool loadIndexTable();
        /**
         * @brief 압축 항목을 풀어 @p outBytes 를 @p uncompressedSize 로 채웁니다. **할당도 여기서 합니다.**
         * @details 엔트리의 원본 크기는 파일에서 온 값입니다. 코덱의 팽창률 상한(`ICompressionCodec::decompressBound`)과 비교한 뒤에만
         *          resize 하도록 할당과 해제를 한 함수에 묶었습니다 — 부르는 쪽이 먼저 resize 하는 모양을 쓸 수 없게 하려는 것입니다(B-5).
         */
        static bool decompressData( PackCompressionType type, const uint8* pSrc, size_t srcSize, size_t uncompressedSize, vector<uint8>& outBytes );
        /**
         * @brief @p other 의 파일 · 헤더 · 인덱스를 넘겨받고 @p other 의 파일 핸들을 비웁니다. 두 쪽의 `_fileMutex` 를 잡은 채로 부릅니다.
         * @details 이동 생성자와 이동 대입이 함께 씁니다. 멤버를 더하면 여기 한 곳만 고칩니다.
         */
        void takeFromLocked( ResourcePakReader& other );

    private:
        mutable mutex                        _fileMutex;
        AsyncFileHandle                      _file; ///< 위치 지정 읽기 핸들(사본을 든 읽기는 닫힌 뒤에도 끝까지 읽는다)
        string                               _packFilePath;
        PackHeader                           _header;
        unordered_map<uint64, PackFileEntry> _mapEntry;
        vector<utf8>                         _stringPoolBytes;
    };

} // namespace sw
