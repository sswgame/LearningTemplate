/**
 * @file PackTypes.h
 * @brief .pack 바이너리 레이아웃(헤더 · FAT 항목)과 팩 열거입니다. **이 헤더가 레이아웃의 원본입니다.**
 * @details 레이아웃은 데이터가 아니라 형식이라 C++ 구조체로 손으로 씁니다. 팩을 쓰는 쪽(`PackWriter`)과 읽는 쪽(`PackReader`)이
 *          같은 구조체를 그대로 쓰고 읽으므로 두 쪽의 오프셋이 어긋날 수 없습니다 — 예전에는 계약 JSON 에서 C++ 헤더와 파이썬 `struct.pack` 이
 *          따로 나와 "마운트는 되는데 파일이 0개" 인 팩이 생길 수 있었습니다. 필드를 고치면 아래 `static_assert` 가 크기 · 오프셋을 다시 못 박게
 *          함께 고치고 `kPackFormatVersion` 을 올립니다(옛 팩은 리더가 거절하고 쿠커가 다시 씁니다).
 *
 *          파일 배치(쓰는 쪽이 정하고 리더는 헤더의 오프셋만 믿는다): 헤더(64B) · 섹터 정렬 FAT(32B × 항목) · 섹터 정렬 페이로드(항목마다 섹터 정렬) ·
 *          섹터 정렬 항목 경로 목록(스트링 풀, NUL 종료 경로를 잇는다). FAT 은 경로 해시 오름차순입니다.
 *          경로 해시는 구분자를 '/' 로 맞춘 팩 안 경로(도메인 제외 — `engine.pack` 의 항목은 `renderpass/default.xml`)의 대소문자 무시
 *          FNV-1a 64(`HashUtil::computeStringHash64` 기본값)입니다.
 *          슬롯 배치(`PackLayoutMode::Slot`)는 헤더 · 섹터 정렬 페이로드 · FAT · 스트링 풀 순서다 — 위 배치와 오프셋 의미는 같고 구역 순서만 다르다(리더는 순서를 가정하지 않는다).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include <cstddef>

namespace sw
{
    /**
     * @enum PackFlag
     * @brief 팩 플래그 비트마스크입니다.
     */
    ENUM( BitFlag )
    enum class PackFlag : uint16
    {
        None          = 0,
        HasStringPool = SW_BIT( 0 ), ///< 항목 경로 목록(스트링 풀)을 담았는지. 폴더 나열이 이 목록을 읽으므로 쿠커는 늘 담고, 리더는 없는 팩을 거절한다
        HasCRC32      = SW_BIT( 1 ), ///< 파일별 CRC32 체크섬을 담았는지
        Encrypted     = SW_BIT( 2 ), ///< FAT 와 페이로드를 암호화했는지
    };

    /**
     * @enum PackCompressionType
     * @brief 팩 안 파일의 압축 코덱 식별자입니다. 코덱은 팩 단위입니다 — 리더가 헤더의 코덱 하나로 모든 항목을 풉니다.
     */
    ENUM()
    enum class PackCompressionType : uint8
    {
        None   = 0, ///< 압축하지 않음(그대로 통과)
        RLE    = 1, ///< 빠른 Run-Length Encoding
        Zlib   = 2, ///< Zlib / Deflate(기본값 — 표준 zlib 리더라 전략 제약이 없다)
        LZ4    = 3, ///< LZ4 초고속 압축(해제가 가장 빠릅니다)
        Zstd   = 4, ///< Zstandard(크기가 가장 작습니다. 배포물용)
        Custom = 255
    };

    /**
     * @enum PackLayoutMode
     * @brief 팩 페이로드 배치 방식입니다(`Config/Engine/PackConfig.json` 의 `_layout._mode`). 디스크 형식은 같다 — 리더는 헤더의 오프셋만 믿는다.
     */
    ENUM()
    enum class PackLayoutMode : uint8
    {
        Sorted = 0, ///< 헤더 · FAT · 페이로드(경로 해시 순으로 이어 붙임) · 스트링 풀. 항목을 더하거나 지우면 뒤 페이로드가 섹터 단위로 밀린다
        Slot        ///< 헤더 · 페이로드 · FAT · 스트링 풀. 앞 빌드의 팩이 있으면 안 바뀐 항목이 앞 빌드의 자리(슬롯)를 지키고 새 항목은 끝에 붙는다
    };

    // **값은 디스크에 박힌다. 새 코덱은 뒤에 덧붙이기만 한다.** 기존 값을 옮기면 이미 쿠킹된 팩을
    // 못 읽는다. 스트림 쪽 `CompressionCodecType` 과는 일부러 값이 다르다(독립된 포맷이다).

    /**
     * @enum PackEncryptionType
     * @brief 팩 암호화 방식 식별자입니다.
     */
    ENUM()
    enum class PackEncryptionType : uint8
    {
        None      = 0, ///< 암호화 없음
        SimpleXor = 1, ///< 빠른 롤링 XOR(난독화)
        AES256GCM = 2, ///< AES-256-GCM(상용 수준 보안)
    };

    /** @brief 'SWPK' 매직 넘버(리틀엔디언)입니다. */
    inline constexpr uint32 kPackMagic = 0x4B505753;

    /** @brief 현재 지원하는 리소스 팩 바이너리 포맷 버전입니다. 레이아웃을 바꾸면 올립니다. */
    inline constexpr uint32 kPackFormatVersion = 1;

    /** @brief DirectStorage 및 NVMe DMA 친화적 섹터 정렬 경계입니다. */
    inline constexpr uint16 kPackSectorAlignment = 4096;

#pragma pack( push, 1 )
    /**
     * @struct PackHeader
     * @brief 64바이트 고정 크기 .pack 파일 헤더입니다.
     */
    struct PackHeader
    {
        uint32 _magic{ 0 };            ///< 'SWPK' 매직 (offset 0)
        uint32 _formatVersion{ 0 };    ///< 포맷 버전 (offset 4)
        uint32 _dlcAppID{ 0 };         ///< DLC 식별자(0: 본편/공용, >0: 유료 DLC AppID) (offset 8)
        uint8  _compressionType{ 0 };  ///< 팩 전체에 적용된 압축 코덱(`PackCompressionType`) (offset 12)
        uint8  _encryptionType{ 0 };   ///< 암호화 방식(`PackEncryptionType`) (offset 13)
        uint16 _sectorAlignment{ 0 };  ///< 섹터 정렬 경계(`kPackSectorAlignment`) (offset 14)
        uint16 _flags{ 0 };            ///< 팩 플래그 비트마스크(`PackFlag`) (offset 16)
        uint32 _fileCount{ 0 };        ///< 팩에 포함된 파일 총 개수 (offset 18)
        uint64 _indexOffset{ 0 };      ///< FAT 인덱스 테이블 시작 오프셋 (offset 22)
        uint64 _indexSize{ 0 };        ///< FAT 인덱스 테이블 바이트 크기 — `_fileCount × sizeof( PackFileEntryOnDisk )` 와 같아야 한다 (offset 30)
        uint64 _stringPoolOffset{ 0 }; ///< 항목 경로 목록(스트링 풀) 시작 오프셋 — 필수다. 런타임 폴더 나열이 이것으로 항목 이름을 안다 (offset 38)
        uint64 _stringPoolSize{ 0 };   ///< 스트링 풀 바이트 크기 (offset 46)
        uint64 _totalDataSize{ 0 };    ///< 정렬 패딩을 포함한 페이로드 총 크기 (offset 54)
        uint8  _reserved[2]{};         ///< 예약 패딩(총 64바이트) (offset 62)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackFileEntryOnDisk
     * @brief 디스크에 저장되는 파일 할당 테이블(FAT) 항목 하나(32바이트)입니다.
     */
    struct PackFileEntryOnDisk
    {
        uint64 _pathHash{ 0 };         ///< 팩 안 경로의 대소문자 무시 FNV-1a 64비트 해시 (offset 0)
        uint64 _dataOffset{ 0 };       ///< 팩 파일 안 페이로드 시작 오프셋(섹터 정렬) (offset 8)
        uint32 _compressedSize{ 0 };   ///< 압축된 데이터 크기(무압축이면 원본 크기) (offset 16)
        uint32 _uncompressedSize{ 0 }; ///< 압축 해제 후 원본 크기 (offset 20)
        uint32 _crc32{ 0 };            ///< 원본 데이터 CRC32 체크섬(빈 데이터는 0) (offset 24)
        uint32 _stringPoolOffset{ 0 }; ///< 스트링 풀 안 항목 경로 오프셋(NUL 종료, 해시가 `_pathHash` 와 같아야 한다) (offset 28)
    };
} // namespace sw

namespace sw
{
#pragma pack( pop )

    // 레이아웃을 못 박는다. 디스크의 바이트가 이 구조체 그대로이므로 크기 · 오프셋이 바뀌면 이미 쿠킹된 팩을 잘못 읽는다.
    static_assert( sizeof( PackHeader ) == 64, "PackHeader must be exactly 64 bytes" );
    static_assert( offsetof( PackHeader, _magic ) == 0 );
    static_assert( offsetof( PackHeader, _formatVersion ) == 4 );
    static_assert( offsetof( PackHeader, _dlcAppID ) == 8 );
    static_assert( offsetof( PackHeader, _compressionType ) == 12 );
    static_assert( offsetof( PackHeader, _encryptionType ) == 13 );
    static_assert( offsetof( PackHeader, _sectorAlignment ) == 14 );
    static_assert( offsetof( PackHeader, _flags ) == 16 );
    static_assert( offsetof( PackHeader, _fileCount ) == 18 );
    static_assert( offsetof( PackHeader, _indexOffset ) == 22 );
    static_assert( offsetof( PackHeader, _indexSize ) == 30 );
    static_assert( offsetof( PackHeader, _stringPoolOffset ) == 38 );
    static_assert( offsetof( PackHeader, _stringPoolSize ) == 46 );
    static_assert( offsetof( PackHeader, _totalDataSize ) == 54 );
    static_assert( offsetof( PackHeader, _reserved ) == 62 );
    static_assert( sizeof( PackFileEntryOnDisk ) == 32, "PackFileEntryOnDisk must be exactly 32 bytes" );
    static_assert( offsetof( PackFileEntryOnDisk, _pathHash ) == 0 );
    static_assert( offsetof( PackFileEntryOnDisk, _dataOffset ) == 8 );
    static_assert( offsetof( PackFileEntryOnDisk, _compressedSize ) == 16 );
    static_assert( offsetof( PackFileEntryOnDisk, _uncompressedSize ) == 20 );
    static_assert( offsetof( PackFileEntryOnDisk, _crc32 ) == 24 );
    static_assert( offsetof( PackFileEntryOnDisk, _stringPoolOffset ) == 28 );
    static_assert( ( kPackSectorAlignment & ( kPackSectorAlignment - 1 ) ) == 0, "sector alignment must be a power of two" );

    /**
     * @struct PackFileEntry
     * @brief 런타임 메모리에 들고 있는 인덱스 항목입니다.
     */
    struct PackFileEntry
    {
        uint64 _pathHash{ 0 };
        uint64 _dataOffset{ 0 };
        uint32 _compressedSize{ 0 };
        uint32 _uncompressedSize{ 0 };
        uint32 _crc32{ 0 };
        uint32 _stringPoolOffset{ 0 };
        string _relativePath; ///< 스트링 풀에서 읽은 팩 안 경로(해시가 `_pathHash` 와 맞는 것을 열 때 확인했다)
    };

} // namespace sw
