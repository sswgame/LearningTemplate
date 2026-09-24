#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Resource/ResourcePackTypes.h"

namespace sw
{
    /**
     * @class ResourcePackReader
     * @brief .pack(SWPK) 바이너리 아카이브 하나를 열고 64비트 해시로 O(1) 파일 읽기를 하는 VFS 리더입니다.
     */
    class SW_API ResourcePackReader
    {
    public:
        ResourcePackReader();
        ~ResourcePackReader();

        ResourcePackReader( const ResourcePackReader& )            = delete;
        ResourcePackReader& operator=( const ResourcePackReader& ) = delete;

        ResourcePackReader( ResourcePackReader&& other ) noexcept;
        ResourcePackReader& operator=( ResourcePackReader&& other ) noexcept;

        /**
         * @brief .pack 파일을 열고 헤더와 FAT 인덱스 테이블을 메모리에 로드합니다.
         * @param packFilePath .pack 파일의 실제 경로
         * @return 유효한 SWPK 아카이브이고 인덱스를 읽었으면 true 입니다.
         */
        bool open( string_view packFilePath );

        /**
         * @brief 열려 있는 팩 파일을 닫고 인덱스 메모리를 해제합니다.
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
         * @brief 팩 안의 파일 데이터를 읽어 CRC32 를 검증하고 압축을 풀어 반환합니다.
         * @param pathHash 64비트 경로 해시
         * @param outBytes 압축을 푼 원본 데이터 버퍼
         * @return 파일을 읽고 CRC32 무결성 검증에 성공하면 true 입니다.
         */
        bool readFile( uint64 pathHash, vector<uint8>& outBytes ) const;

        /** @brief 가상 상대 경로로 파일을 읽습니다. */
        bool readFile( string_view relativePath, vector<uint8>& outBytes ) const;

        /** @brief 가상 상대 경로로 텍스트 파일(UTF-8)을 읽습니다. */
        bool readTextFile( string_view relativePath, string& outText ) const;

        /** @brief 팩 헤더를 반환합니다. */
        const PackHeader& getHeader() const;

        /** @brief DLC 식별자를 반환합니다(0 = 본편, >0 = DLC AppID). */
        uint32 getDlcAppId() const;

        /** @brief 팩에 든 파일 총 개수를 반환합니다. */
        uint32 getFileCount() const;

        /** @brief 연 .pack 파일의 실제 경로를 반환합니다. */
        const string& getPackPath() const;

    private:
        /**
         * @brief 헤더가 말하는 인덱스 · 스트링 풀 구역이 **실제 파일 안에** 있는지 확인합니다.
         * @details 헤더의 수는 파일에서 온 값입니다. 그것을 그대로 믿고 `resize` 하면 손상된 팩
         *          하나가 거대한 할당 요청이 됩니다. `_indexSize` 와 `_fileCount` 가 같은 것을
         *          두 번 말하는 것도 여기서 맞춰 봅니다.
         * @param outFileSize 실제 파일 크기입니다. 항목 검사(`validateFileEntry`)가 같은 값을 씁니다.
         */
        bool validateHeaderGeometry( uint64& outFileSize ) const;
        /** @brief FAT 항목 하나가 파일 안을 가리키는지, 크기가 다룰 만한지 봅니다. */
        bool validateFileEntry( const PackFileEntryOnDisk& diskEntry, uint64 fileSize ) const;
        bool loadIndexTable();
        bool decompressData( PackCompressionType type, const uint8* pSrc, size_t srcSize, void* pDst, size_t dstSize ) const;
        /**
         * @brief @p other 의 파일 · 헤더 · 인덱스를 넘겨받고 @p other 의 파일 핸들을 비웁니다. 두 쪽의 `_fileMutex` 를 잡은 채로 부릅니다.
         * @details 이동 생성자와 이동 대입이 같은 여섯 줄을 각자 들고 있었습니다. 멤버를 하나 더하면 두 곳을 다 고쳐야 했습니다.
         */
        void takeFromLocked( ResourcePackReader& other );

    private:
        mutable mutex                        _fileMutex;
        void*                                _pFileHandle; ///< 내부 FILE* 포인터
        string                               _packFilePath;
        PackHeader                           _header;
        unordered_map<uint64, PackFileEntry> _mapEntry;
        vector<utf8>                         _stringPoolBytes;
    };

} // namespace sw
