#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 'SWPK' (Little Endian: 'S', 'W', 'P', 'K') 매직 넘버 */
    inline constexpr uint32 kPackMagic = 0x4B505753;

    /** @brief 현재 지원하는 리소스 팩 바이너리 포맷 버전 */
    inline constexpr uint32 kPackFormatVersion = 1;

    /** @brief DirectStorage 및 NVMe DMA 친화적 4KB 섹터 정렬 경계 */
    inline constexpr uint16 kPackSectorAlignment = 4096;

    /**
     * @enum PackFlag
     * @brief 팩 플래그 비트마스크
     */
    ENUM( BitFlag )
    enum class PackFlag : uint16
    {
        None          = 0,
        HasStringPool = SW_BIT( 0 ), ///< 디버그용 원본 경로 스트링 풀 포함 여부 (Shipping 시 미포함)
        HasCrc32      = SW_BIT( 1 ), ///< 파일별 CRC32 체크섬 포함 여부
        Encrypted     = SW_BIT( 2 ), ///< FAT 및 페이로드 암호화 여부
    };

    /**
     * @enum PackCompressionType
     * @brief 팩 내부 파일 압축 코덱 식별자
     */
    ENUM()
    enum class PackCompressionType : uint8
    {
        None   = 0, ///< 무압축 (Pass-through)
        RLE    = 1, ///< 고속 Run-Length Encoding
        Zlib   = 2, ///< Zlib / Deflate 압축 (표준)
        LZ4    = 3, ///< LZ4 초고속 압축
        Custom = 255
    };

    /**
     * @enum PackEncryptionType
     * @brief 팩 암호화 방식 식별자
     */
    ENUM()
    enum class PackEncryptionType : uint8
    {
        None      = 0, ///< 암호화 없음
        SimpleXor = 1, ///< 고속 롤링 XOR (난독화)
        AES256GCM = 2, ///< AES-256-GCM (상용 보안)
    };

#pragma pack( push, 1 )
    /**
     * @struct PackHeader
     * @brief 64바이트 고정 크기 .pack 파일 헤더
     */
    struct PackHeader
    {
        uint32 _magic{ kPackMagic };                                                ///< 'SWPK' 매직 (4B)
        uint32 _formatVersion{ kPackFormatVersion };                                ///< 포맷 버전 (4B)
        uint32 _dlcAppId{ 0 };                                                      ///< DLC 식별자 (0: 본편/공용, >0: 유료 DLC AppID) (4B)
        uint8  _compressionType{ static_cast<uint8>( PackCompressionType::None ) }; ///< 압축 방식 (1B)
        uint8  _encryptionType{ static_cast<uint8>( PackEncryptionType::None ) };   ///< 암호화 방식 (1B)
        uint16 _sectorAlignment{ kPackSectorAlignment };                            ///< 섹터 정렬 경계 (기본 4096) (2B)
        uint16 _flags{ static_cast<uint16>( PackFlag::HasCrc32 ) };                 ///< 팩 플래그 비트마스크 (2B)
        uint32 _fileCount{ 0 };                                                     ///< 팩에 포함된 파일 총 개수 (4B)
        uint64 _indexOffset{ 0 };                                                   ///< FAT 인덱스 테이블 파일 시작 오프셋 (8B)
        uint64 _indexSize{ 0 };                                                     ///< FAT 인덱스 테이블 바이트 크기 (8B)
        uint64 _stringPoolOffset{ 0 };                                              ///< 디버그용 원본 경로 스트링 풀 시작 오프셋 (8B)
        uint64 _stringPoolSize{ 0 };                                                ///< 스트링 풀 바이트 크기 (8B)
        uint64 _totalDataSize{ 0 };                                                 ///< 정렬 패딩을 포함한 페이로드 총 크기 (8B)
        uint8  _reserved[2]{ 0 };                                                   ///< 예약 패딩 (총 64바이트 정렬) (2B)
    };
    static_assert( sizeof( PackHeader ) == 64, "PackHeader must be exactly 64 bytes" );

    /**
     * @struct PackFileEntryOnDisk
     * @brief 디스크에 저장되는 파일 할당 테이블(FAT) 개별 항목 (32바이트 고정)
     */
    struct PackFileEntryOnDisk
    {
        uint64 _pathHash{ 0 };         ///< 소문자 가상 경로의 FNV-1a 64비트 해시 (8B)
        uint64 _dataOffset{ 0 };       ///< 팩 파일 내 페이로드 시작 오프셋 (4KB 정렬) (8B)
        uint32 _compressedSize{ 0 };   ///< 압축된 데이터 크기 (4B)
        uint32 _uncompressedSize{ 0 }; ///< 압축 해제 후 원본 크기 (4B)
        uint32 _crc32{ 0 };            ///< 원본 데이터 CRC32 체크섬 (4B)
        uint32 _stringPoolOffset{ 0 }; ///< 스트링 풀 내 원본 경로 오프셋 (스트링 풀 없을 시 0) (4B)
    };
    static_assert( sizeof( PackFileEntryOnDisk ) == 32, "PackFileEntryOnDisk must be exactly 32 bytes" );
#pragma pack( pop )

    /**
     * @struct PackFileEntry
     * @brief 런타임 메모리에서 관리하는 인덱스 엔트리
     */
    struct PackFileEntry
    {
        uint64 _pathHash{ 0 };
        uint64 _dataOffset{ 0 };
        uint32 _compressedSize{ 0 };
        uint32 _uncompressedSize{ 0 };
        uint32 _crc32{ 0 };
        uint32 _stringPoolOffset{ 0 };
        string _debugRelativePath; ///< 디버그 스트링 풀에서 읽은 원본 경로 (Shipping 시 빈 문자열)
    };

} // namespace sw
