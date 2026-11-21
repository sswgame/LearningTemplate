/**
 * @file PackTypes.h
 * @brief .pack 바이너리 레이아웃(헤더 · FAT 항목)과 팩 열거입니다. **이 헤더가 레이아웃의 원본입니다.**
 * @details 레이아웃은 데이터가 아니라 형식이라 C++ 구조체로 손으로 씁니다. 팩을 쓰는 쪽(`PackStreamWriter`)과 읽는 쪽(`PackReader`)이
 *          같은 구조체를 그대로 쓰고 읽으므로 두 쪽의 오프셋이 어긋날 수 없습니다 — 예전에는 계약 JSON 에서 C++ 헤더와 파이썬 `struct.pack` 이
 *          따로 나와 "마운트는 되는데 파일이 0개" 인 팩이 생길 수 있었습니다. 필드를 고치면 아래 `static_assert` 가 크기 · 오프셋을 다시 못 박게
 *          함께 고치고 `kPackFormatVersion` 을 올립니다(옛 팩은 리더가 거절하고 쿠커가 다시 씁니다).
 *
 *          파일 배치(형식 3, 쓰는 쪽이 정하고 리더는 헤더의 오프셋만 믿는다): 페이로드(오프셋 0 부터, 항목마다 섹터 정렬) · 섹터 정렬 FAT(32B × 항목) ·
 *          블록 표(8B × 블록) · (블록 해시 표, 32B × 블록 — `HasBlockHash` 일 때) · 항목 경로 목록(스트링 풀, NUL 종료 경로를 잇는다) · 헤더(96B, **파일 끝**). FAT 은 경로 해시 오름차순이고 리더는 아니면 거절한다.
 *          헤더를 끝에 두는 것은 언리얼 pak(`FPakInfo` 가 파일 끝)과 같은 자리다 — 스트림 쓰기(`PackStreamWriter`)가 앞에서부터 한 번 쓰면서 파일 내용 해시를
 *          함께 세려면 마지막에 정해지는 칸(오프셋 · 수)이 앞에 있으면 안 된다. 색인 구역(FAT · 블록 표 · 블록 해시 표 · 스트링 풀 · 헤더)은 `_indexOffset` 부터 파일 끝까지 빈틈 없는 한 구간이고(리더가 확인한다)
 *          패치 서명은 이 구간의 SHA-256 에 건다(PK-5).
 *          모든 항목은 `_blockSize`(압축 전) 블록으로 잘려 블록마다 압축되고 CRC32 는 블록 표에 있다 — 범위 읽기(`PackReader::readRange`)가 걸친 블록만 읽는다.
 *          경로 해시는 구분자를 '/' 로 맞춘 팩 안 경로(도메인 제외 — `engine.pack` 의 항목은 `pipeline/forwardpipeline.xml`)의 대소문자 무시
 *          FNV-1a 64(`HashUtil::computeStringHash64` 기본값)입니다. `PackLayoutMode` 는 페이로드 자리 규칙일 뿐이다(구역 순서는 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/BitUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include <cstddef>

namespace sw
{
    /**
     * @enum PackFlag
     * @brief 팩 플래그 비트마스크입니다.
     */
    ENUM( Flags )
    enum class PackFlag : uint16
    {
        None          = 0,
        HasStringPool = BitUtil::bit( 0 ), ///< 항목 경로 목록(스트링 풀)을 담았는지. 폴더 나열이 이 목록을 읽으므로 쿠커는 늘 담고, 리더는 없는 팩을 거절한다
        HasCRC32      = BitUtil::bit( 1 ), ///< 블록별 CRC32(블록 표의 `_crc32`)를 읽을 때 검사하는지
        Encrypted     = BitUtil::bit( 2 ), ///< FAT 와 페이로드를 암호화했는지
        HasBlockHash  = BitUtil::bit( 3 ), ///< 블록 해시 표(블록마다 저장 바이트의 SHA-256)를 담았는지 — 서명 채널(패치 팩)의 팩. 신뢰를 받아 연 리더가 읽을 때 블록마다 견준다
    };

    /**
     * @enum PackCompressionKind
     * @brief 팩 안 파일의 압축 코덱 식별자입니다. 코덱은 팩 단위입니다 — 리더가 헤더의 코덱 하나로 모든 항목을 풉니다.
     */
    ENUM()
    enum class PackCompressionKind : uint8
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
        Sorted = 0, ///< 페이로드를 경로 해시 순으로 이어 붙인다. 항목을 더하거나 지우면 뒤 페이로드가 섹터 단위로 밀린다
        Slot        ///< 앞 빌드의 팩이 있으면 안 바뀐 항목이 앞 빌드의 자리(슬롯)를 지키고 새 항목 · 커진 항목은 앞 페이로드 끝 뒤에 붙는다
    };

    /**
     * @enum PackEntryStorage
     * @brief 항목 하나의 저장 방식입니다(FAT 칸 `_placement` 의 위 8 비트). 언리얼 pak 항목의 압축 방식 번호 자리다.
     * @details 이미 압축된 형식(BCn `.dds` · `.ktx2` · 이미지 · 오디오)은 다시 압축해도 줄지 않고 범위 읽기마다 풀기만 든다 — `Stored` 로 담으면 범위 읽기가
     *          풀기 없이 그 블록의 바이트를 그대로 준다(첫 로드의 머리 + 꼬리 읽기). 블록 · CRC · 블록 요약은 두 방식이 같다. 쿠커가 항목마다 정한다(PackConfig `_listStoredExtension`).
     *          **값은 디스크에 박힌다. 새 방식은 뒤에 덧붙이고 `kPackEntryStorageCount` 를 올린다** — 리더는 그 밖의 값을 거절한다.
     */
    ENUM()
    enum class PackEntryStorage : uint8
    {
        Blocks = 0, ///< 블록마다 팩 코덱으로 압축한다(코덱이 `None` 이면 그대로)
        Stored = 1, ///< 압축하지 않는다 — 블록의 저장 바이트가 곧 원본이다
    };
    /** @brief `PackEntryStorage` 값의 수입니다(리더가 범위를 본다). */
    inline constexpr uint8 kPackEntryStorageCount = 2;

    // **값은 디스크에 박힌다. 새 코덱은 뒤에 덧붙이기만 한다.** 기존 값을 옮기면 이미 쿠킹된 팩을
    // 못 읽는다. 스트림 쪽 `CompressionCodecKind` 과는 일부러 값이 다르다(독립된 포맷이다).

    /**
     * @enum PackEncryptionKind
     * @brief 팩 암호화 방식 식별자입니다.
     */
    ENUM()
    enum class PackEncryptionKind : uint8
    {
        None      = 0, ///< 암호화 없음
        SimpleXor = 1, ///< 빠른 롤링 XOR(난독화)
        AES256GCM = 2, ///< AES-256-GCM(상용 수준 보안)
    };

    /** @brief 'SWPK' 매직 넘버(리틀엔디언)입니다. */
    inline constexpr uint32 kPackMagic = 0x4B505753;

    /**
     * @brief 현재 지원하는 리소스 팩 바이너리 포맷 버전입니다. 레이아웃을 바꾸면 올립니다.
     * @details 2: 스트링 풀의 맨 앞(오프셋 0)이 마운트 도메인(NUL 종료, 언리얼 pak mount point)이다 — `engine` · `game/playground` · `dlc/winter`, 패치 팩은 빈 글(항목이 전체 경로).
     *          예전에는 팩 파일 이름(`game_playground.pack`)에서 도메인을 추측했다.
     *          3: 헤더가 파일 끝(96B)이고 블록 표가 생겼다. FAT 칸 `_crc32` 자리가 `_firstBlock` 이 되고 CRC 는 블록마다다. 옛 판은 리더가 거절하고 쿠커가 다시 쓴다
     *          (판은 팩 쿠킹 설정 해시에 들어가 옛 산출물을 재사용하지 않는다).
     */
    inline constexpr uint32 kPackFormatVersion = 3;

    /** @brief 블록 크기(압축 전)의 형식 한계입니다. 쓰는 쪽은 이 안의 2 의 거듭제곱만 쓰고(`PackConfig` 의 `_blockSize`) 리더는 밖의 값을 거절합니다. */
    inline constexpr uint32 kPackMinBlockSize = 64 * constant::memory::kBytesPerKilobyte;
    inline constexpr uint32 kPackMaxBlockSize = kPackMinBlockSize * 4;

    /** @brief 색인 · 블록 요약(SHA-256)의 바이트 수입니다. */
    inline constexpr uint32 kPackDigestSize = 32;

    /** @struct PackDigest @brief SHA-256 요약 하나입니다(블록 해시 표의 줄 · 색인 구역의 요약). 공개 값이라 비교는 일반 비교다. */
    struct PackDigest
    {
        uint8 _arrByte[kPackDigestSize]{};

        bool operator==( const PackDigest& other ) const
        {
            for ( uint32 index = 0; index < kPackDigestSize; ++index )
            {
                if ( _arrByte[index] != other._arrByte[index] )
                    return false;
            }
            return true;
        }
        bool operator!=( const PackDigest& other ) const { return ( *this == other ) == false; }
    };
    static_assert( sizeof( PackDigest ) == kPackDigestSize, "PackDigest must be exactly the digest bytes" );

    /** @brief DirectStorage 및 NVMe DMA 친화적 섹터 정렬 경계입니다. */
    inline constexpr uint16 kPackSectorAlignment = 4096;

#pragma pack( push, 1 )
    /**
     * @struct PackHeader
     * @brief 96바이트 고정 크기 .pack 파일 헤더입니다. **파일의 마지막 96 바이트**입니다(파일 머리말).
     */
    struct PackHeader
    {
        uint32 _magic{ 0 };            ///< 'SWPK' 매직 (offset 0)
        uint32 _formatVersion{ 0 };    ///< 포맷 버전 (offset 4)
        uint32 _dlcAppID{ 0 };         ///< DLC 식별자(0: 본편/공용, >0: 유료 DLC AppID) (offset 8)
        uint8  _compressionType{ 0 };  ///< 팩 전체에 적용된 압축 코덱(`PackCompressionKind`) (offset 12)
        uint8  _encryptionType{ 0 };   ///< 암호화 방식(`PackEncryptionKind`) (offset 13)
        uint16 _sectorAlignment{ 0 };  ///< 섹터 정렬 경계(`kPackSectorAlignment`) (offset 14)
        uint16 _flags{ 0 };            ///< 팩 플래그 비트마스크(`PackFlag`) (offset 16)
        uint32 _fileCount{ 0 };        ///< 팩에 포함된 파일 총 개수 (offset 18)
        uint64 _indexOffset{ 0 };      ///< FAT 시작 오프셋 — 색인 구역(FAT · 블록 표 · 스트링 풀 · 헤더)의 시작이기도 하다 (offset 22)
        uint64 _indexSize{ 0 };        ///< FAT 인덱스 테이블 바이트 크기 — `_fileCount × sizeof( PackFileEntryOnDisk )` 와 같아야 한다 (offset 30)
        uint64 _stringPoolOffset{ 0 }; ///< 항목 경로 목록(스트링 풀) 시작 오프셋 — 필수다. 런타임 폴더 나열이 이것으로 항목 이름을 안다 (offset 38)
        uint64 _stringPoolSize{ 0 };   ///< 스트링 풀 바이트 크기 (offset 46)
        uint64 _totalDataSize{ 0 };    ///< 정렬 패딩을 포함한 페이로드 구역 크기(오프셋 0 부터) (offset 54)
        uint64 _blockTableOffset{ 0 }; ///< 블록 표(`PackBlockEntryOnDisk` × `_blockCount`) 시작 오프셋 (offset 62)
        uint64 _blockHashOffset{ 0 };  ///< 블록 해시 표(`PackDigest` × `_blockCount`) 시작 — `PackFlag::HasBlockHash` 일 때만, 아니면 0 (offset 70)
        uint32 _blockCount{ 0 };       ///< 블록 표의 줄 수 — 항목마다 `ceil( _uncompressedSize / _blockSize )` 의 합 (offset 78)
        uint32 _blockSize{ 0 };        ///< 압축 전 블록 크기(`kPackMinBlockSize` ~ `kPackMaxBlockSize` 의 2 의 거듭제곱) (offset 82)
        uint8  _reserved[10]{};        ///< 예약 패딩(총 96바이트) (offset 86)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct PackFileEntryOnDisk
     * @brief 디스크에 저장되는 파일 할당 테이블(FAT) 항목 하나(32바이트)입니다.
     * @details 페이로드 오프셋과 저장 방식은 한 칸(`_placement`)에 함께 든다 — 오프셋 56 비트(64 PiB)면 넉넉하고 항목이 32 바이트로 남는다. 칸은 접근 함수로만 읽고 쓴다.
     */
    struct PackFileEntryOnDisk
    {
        /** @brief `_placement` 에서 오프셋이 차지하는 아래 비트 수입니다. */
        static constexpr uint32 kDataOffsetBits = 56;
        /** @brief 오프셋이 담길 수 있는 가장 큰 값입니다. */
        static constexpr uint64 kMaxDataOffset = ( uint64{ 1 } << kDataOffsetBits ) - 1;

        uint64 _pathHash{ 0 };         ///< 팩 안 경로의 대소문자 무시 FNV-1a 64비트 해시 (offset 0)
        uint64 _placement{ 0 };        ///< 아래 56 비트 = 페이로드 시작 오프셋, 위 8 비트 = 저장 방식(`PackEntryStorage`) (offset 8)
        uint32 _compressedSize{ 0 };   ///< 압축된 데이터 크기(무압축이면 원본 크기) (offset 16)
        uint32 _uncompressedSize{ 0 }; ///< 압축 해제 후 원본 크기 (offset 20)
        uint32 _firstBlock{ 0 };       ///< 블록 표 안 이 항목의 첫 블록. 블록 수는 `ceil( _uncompressedSize / _blockSize )`(빈 항목은 0)이고 FAT 순서대로 이어진다 (offset 24)
        uint32 _stringPoolOffset{ 0 }; ///< 스트링 풀 안 항목 경로 오프셋(NUL 종료, 해시가 `_pathHash` 와 같아야 한다) (offset 28)

        /** @brief 페이로드 시작 오프셋입니다. */
        uint64 getDataOffset() const { return _placement & kMaxDataOffset; }
        /** @brief 저장 방식의 디스크 값입니다 — 리더가 `kPackEntryStorageCount` 안인지 본 뒤에 `PackEntryStorage` 로 읽는다. */
        uint8 getStorageValue() const { return static_cast<uint8>( _placement >> kDataOffsetBits ); }
        /** @brief 오프셋과 저장 방식을 적습니다. @p dataOffset 은 `kMaxDataOffset` 이하여야 한다(쓰는 쪽이 먼저 본다). */
        void setPlacement( uint64 dataOffset, PackEntryStorage storage ) { _placement = ( dataOffset & kMaxDataOffset ) | ( static_cast<uint64>( storage ) << kDataOffsetBits ); }
    };

    /**
     * @struct PackBlockEntryOnDisk
     * @brief 블록 표 한 줄(8바이트)입니다. 항목의 블록 i 는 [끝(i-1), 끝(i)) 에 있다(블록 0 의 앞 끝은 0) — 자리를 O(1) 로 안다.
     */
    struct PackBlockEntryOnDisk
    {
        uint32 _compressedEnd{ 0 }; ///< 항목 페이로드 시작 기준 이 블록의 저장 바이트 누적 끝 (offset 0)
        uint32 _crc32{ 0 };         ///< 압축 전 블록의 CRC32 (offset 4)
    };
} // namespace sw

namespace sw
{
#pragma pack( pop )

    // 레이아웃을 못 박는다. 디스크의 바이트가 이 구조체 그대로이므로 크기 · 오프셋이 바뀌면 이미 쿠킹된 팩을 잘못 읽는다.
    static_assert( sizeof( PackHeader ) == 96, "PackHeader must be exactly 96 bytes" );
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
    // 형식 3 에서 더한 칸은 바로 앞 칸 뒤에 빈틈 없이 붙는다.
    static_assert( offsetof( PackHeader, _blockTableOffset ) == offsetof( PackHeader, _totalDataSize ) + sizeof( uint64 ) );
    static_assert( offsetof( PackHeader, _blockHashOffset ) == offsetof( PackHeader, _blockTableOffset ) + sizeof( uint64 ) );
    static_assert( offsetof( PackHeader, _blockCount ) == offsetof( PackHeader, _blockHashOffset ) + sizeof( uint64 ) );
    static_assert( offsetof( PackHeader, _blockSize ) == offsetof( PackHeader, _blockCount ) + sizeof( uint32 ) );
    static_assert( offsetof( PackHeader, _reserved ) == offsetof( PackHeader, _blockSize ) + sizeof( uint32 ) );
    static_assert( sizeof( PackFileEntryOnDisk ) == 32, "PackFileEntryOnDisk must be exactly 32 bytes" );
    static_assert( offsetof( PackFileEntryOnDisk, _pathHash ) == 0 );
    static_assert( offsetof( PackFileEntryOnDisk, _placement ) == 8 );
    static_assert( offsetof( PackFileEntryOnDisk, _compressedSize ) == 16 );
    static_assert( offsetof( PackFileEntryOnDisk, _uncompressedSize ) == 20 );
    static_assert( offsetof( PackFileEntryOnDisk, _firstBlock ) == 24 );
    static_assert( offsetof( PackFileEntryOnDisk, _stringPoolOffset ) == 28 );
    static_assert( sizeof( PackBlockEntryOnDisk ) == sizeof( uint64 ), "PackBlockEntryOnDisk must be exactly 8 bytes" );
    static_assert( offsetof( PackBlockEntryOnDisk, _compressedEnd ) == 0 );
    static_assert( offsetof( PackBlockEntryOnDisk, _crc32 ) == sizeof( uint32 ) );
    static_assert( ( kPackMinBlockSize & ( kPackMinBlockSize - 1 ) ) == 0 && ( kPackMaxBlockSize & ( kPackMaxBlockSize - 1 ) ) == 0, "block size limits must be powers of two" );
    static_assert( ( kPackSectorAlignment & ( kPackSectorAlignment - 1 ) ) == 0, "sector alignment must be a power of two" );

    /**
     * @struct PackFileEntry
     * @brief 항목 하나의 조회 결과(`PackReader::getFileEntry`)입니다. 리더는 디스크 FAT 을 그대로 들고, 경로는 조회할 때 스트링 풀에서 복사합니다.
     */
    struct PackFileEntry
    {
        uint64           _pathHash{ 0 };
        uint64           _dataOffset{ 0 };
        uint32           _compressedSize{ 0 };
        uint32           _uncompressedSize{ 0 };
        uint32           _firstBlock{ 0 };
        PackEntryStorage _storage{ PackEntryStorage::Blocks };
        string           _relativePath; ///< 스트링 풀에서 읽은 팩 안 경로(해시가 `_pathHash` 와 맞는 것을 열 때 확인했다)
    };

} // namespace sw
