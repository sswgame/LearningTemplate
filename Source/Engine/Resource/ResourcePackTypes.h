#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "sw/config/PackFormat.gen.h"

// .pack 매직/버전/정렬 상수와 온디스크 구조체(PackHeader, PackFileEntryOnDisk)는
// Config/Engine/PackFormat.json 에서 생성된다. 같은 계약 파일을 Python 쿠커가 읽으므로
// 레이아웃을 손으로 맞출 일이 없다. 구조체를 고치려면 계약 파일을 고친다.

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
        HasStringPool = SW_BIT( 0 ), ///< 디버그용 원본 경로 스트링 풀을 담았는지(Shipping 은 담지 않음)
        HasCrc32      = SW_BIT( 1 ), ///< 파일별 CRC32 체크섬을 담았는지
        Encrypted     = SW_BIT( 2 ), ///< FAT 와 페이로드를 암호화했는지
    };

    /**
     * @enum PackCompressionType
     * @brief 팩 안 파일의 압축 코덱 식별자입니다.
     */
    ENUM()
    enum class PackCompressionType : uint8
    {
        None   = 0, ///< 압축하지 않음(그대로 통과)
        RLE    = 1, ///< 빠른 Run-Length Encoding
        Zlib   = 2, ///< Zlib / Deflate(쿠커가 파이썬이라 기본값입니다. 표준 라이브러리에 들어 있습니다)
        LZ4    = 3, ///< LZ4 초고속 압축(해제가 가장 빠릅니다)
        Zstd   = 4, ///< Zstandard(크기가 가장 작습니다. 배포물용)
        Custom = 255
    };

    // **값은 디스크에 박힌다. 새 코덱은 뒤에 덧붙이기만 한다.** 기존 값을 옮기면 이미 구운 팩을
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

    // 온디스크 구조체는 생성 헤더(PackFormat.gen.h)에 있다. enum 값이 계약 파일과 어긋나면
    // 쿠커가 쓴 값과 리더가 해석하는 값이 달라지므로 여기서 못 박는다.
    static_assert( static_cast<uint32>( PackCompressionType::None ) == packformat::kCompressionNone );
    static_assert( static_cast<uint32>( PackCompressionType::RLE ) == packformat::kCompressionRLE );
    static_assert( static_cast<uint32>( PackCompressionType::Zlib ) == packformat::kCompressionZlib );
    static_assert( static_cast<uint32>( PackCompressionType::LZ4 ) == packformat::kCompressionLZ4 );
    static_assert( static_cast<uint32>( PackCompressionType::Zstd ) == packformat::kCompressionZstd );
    static_assert( static_cast<uint32>( PackEncryptionType::None ) == packformat::kEncryptionNone );
    static_assert( static_cast<uint32>( PackEncryptionType::SimpleXor ) == packformat::kEncryptionSimpleXor );
    static_assert( static_cast<uint32>( PackEncryptionType::AES256GCM ) == packformat::kEncryptionAES256GCM );
    static_assert( static_cast<uint32>( PackFlag::HasStringPool ) == packformat::kFlagHasStringPool );
    static_assert( static_cast<uint32>( PackFlag::HasCrc32 ) == packformat::kFlagHasCrc32 );
    static_assert( static_cast<uint32>( PackFlag::Encrypted ) == packformat::kFlagEncrypted );

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
        string _debugRelativePath; ///< 디버그 스트링 풀에서 읽은 원본 경로(Shipping 은 빈 문자열)
    };

} // namespace sw
