#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @enum CompressionCodecType
     * @brief 지원되는 압축 코덱 식별자
     */
    enum class CompressionCodecType : uint8
    {
        None   = 0,   ///< 무압축 (Pass-through)
        RLE    = 1,   ///< 고속 바이트 런렝스 압축
        LZ4    = 2,   ///< LZ4 — 해제가 가장 빠르다 (로딩 시간이 곧 해제 시간인 자리)
        Zstd   = 3,   ///< Zstandard — 크기가 가장 작다 (배포물)
        Zlib   = 4,   ///< Deflate — 굽는 쪽이 파이썬이라 리소스 팩이 쓴다
        Custom = 255, ///< 사용자 정의 코덱
    };

    // **이 값은 디스크에 기록된다** (`CompressionStream` 컨테이너 헤더의 `_codecType`). 기존 값을 옮기면
    // 예전에 쓴 스트림을 못 읽는다 — **새 코덱은 뒤에 덧붙이기만 한다.**
    //
    // 리소스 팩의 `PackCompressionType` 과는 **일부러 값이 다르다.** 둘은 서로 다른 파일의 독립된
    // 포맷이라, 숫자를 맞춰 두면 `static_cast` 로 건너다니고 싶어지고 그 순간 한쪽 포맷 변경이 다른
    // 쪽을 끌고 간다. 코덱 **구현**은 공유하되(같은 `ICompressionCodec` 클래스들), 어떤 코덱을 쓸지는
    // 각 포맷이 자기 enum 으로 스스로 고른다.

    /**
     * @class ICompressionCodec
     * @brief 압축/압축해제 알고리즘 인터페이스
     * @details 특정 압축 라이브러리(LZ4, Zstd 등)에 종속되지 않도록 인터페이스와 구현을 완전히 분리합니다.
     */
    class SW_API ICompressionCodec
    {
    public:
        ICompressionCodec()                                          = default;
        virtual ~ICompressionCodec()                                 = default;
        ICompressionCodec( const ICompressionCodec& )                = default;
        ICompressionCodec& operator=( const ICompressionCodec& )     = default;
        ICompressionCodec( ICompressionCodec&& ) noexcept            = default;
        ICompressionCodec& operator=( ICompressionCodec&& ) noexcept = default;

        /**
         * @brief 코덱 유형을 반환합니다.
         */
        virtual CompressionCodecType getCodecType() const = 0;

        /**
         * @brief 사람이 읽을 수 있는 코덱 이름을 반환합니다.
         */
        virtual const utf8* getCodecName() const = 0;

        /**
         * @brief 주어진 원본 크기에 대해 압축 시 최악의 경우 필요한 최대 버퍼 크기를 계산합니다.
         */
        virtual size_t compressBound( size_t uncompressedSize ) const = 0;

        /**
         * @brief 데이터를 압축합니다.
         * @param pSrc 원본 데이터 포인터
         * @param srcSize 원본 데이터 크기 (바이트)
         * @param pDst 압축 데이터를 저장할 대상 버퍼 포인터
         * @param dstCapacity 대상 버퍼 용량
         * @param outCompressedSize 실제 압축되어 기록된 바이트 크기
         * @param compressionLevel 압축 레벨 (기본값: 0)
         * @return 압축 성공 여부
         */
        virtual bool compress( const void* pSrc,
                               size_t      srcSize,
                               void*       pDst,
                               size_t      dstCapacity,
                               size_t&     outCompressedSize,
                               int32       compressionLevel = 0 ) = 0;

        /**
         * @brief 압축된 데이터를 해제(복원)합니다.
         * @param pSrc 압축 데이터 포인터
         * @param srcSize 압축 데이터 크기 (바이트)
         * @param pDst 복원 데이터를 저장할 대상 버퍼 포인터
         * @param dstCapacity 대상 버퍼 용량
         * @param outUncompressedSize 실제 복원되어 기록된 바이트 크기
         * @return 압축 해제 성공 여부
         */
        virtual bool decompress( const void* pSrc,
                                 size_t      srcSize,
                                 void*       pDst,
                                 size_t      dstCapacity,
                                 size_t&     outUncompressedSize ) = 0;
    };
} // namespace sw
