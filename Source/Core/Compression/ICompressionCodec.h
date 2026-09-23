/**
 * @file ICompressionCodec.h
 * @brief 압축 코덱 인터페이스와 코덱 식별자입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @enum CompressionCodecType
     * @brief 지원하는 압축 코덱의 식별자입니다.
     * @details 이 값은 디스크에 기록됩니다(`CompressionStream` 컨테이너 헤더의 `_codecType`). 기존 값을 바꾸면 예전에 쓴 스트림을
     *          읽지 못하므로, 새 코덱은 **뒤에 덧붙이기만** 합니다.
     *
     *          리소스 팩의 `PackCompressionType` 과는 일부러 값을 다르게 두었습니다. 둘은 서로 다른 파일의 독립된 포맷입니다.
     *          숫자를 맞춰 두면 `static_cast` 로 오가고 싶어지고, 그 순간부터 한쪽의 포맷 변경이 다른 쪽까지 끌고 갑니다.
     *          코덱 **구현**은 공유하되(같은 `ICompressionCodec` 클래스들), 어떤 코덱을 쓸지는 각 포맷이 자기 enum 으로 정합니다.
     */
    enum class CompressionCodecType : uint8
    {
        None   = 0,   ///< 무압축(그대로 복사)
        RLE    = 1,   ///< 빠른 바이트 단위 RLE
        LZ4    = 2,   ///< LZ4. 해제가 가장 빠르다(로딩 시간이 곧 해제 시간인 곳)
        Zstd   = 3,   ///< Zstandard. 결과가 가장 작다(배포물)
        Zlib   = 4,   ///< Deflate. 리소스 팩을 굽는 도구가 파이썬(표준 zlib)이라 팩 포맷이 쓴다
        Custom = 255, ///< 사용자 정의 코덱
    };

    /**
     * @class ICompressionCodec
     * @brief 압축 · 해제 알고리즘의 인터페이스입니다.
     * @details 특정 압축 라이브러리(LZ4, Zstd 등)에 묶이지 않도록 인터페이스와 구현을 분리합니다.
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
         * @brief 원본 크기가 uncompressedSize 일 때 압축 결과가 최악의 경우 차지할 수 있는 크기를 구합니다.
         */
        virtual size_t compressBound( size_t uncompressedSize ) const = 0;

        /**
         * @brief 데이터를 압축합니다.
         * @param pSrc 원본 데이터
         * @param srcSize 원본 데이터 크기(바이트)
         * @param pDst 압축 결과를 쓸 버퍼
         * @param dstCapacity 대상 버퍼 용량(바이트)
         * @param outCompressedSize 실제로 쓴 압축 데이터 크기(바이트)
         * @param compressionLevel 압축 레벨(기본값 0)
         * @return 성공하면 true
         */
        virtual bool compress( const void* pSrc,
                               size_t      srcSize,
                               void*       pDst,
                               size_t      dstCapacity,
                               size_t&     outCompressedSize,
                               int32       compressionLevel = 0 ) = 0;

        /**
         * @brief 압축된 데이터를 풀어 원본으로 복원합니다.
         * @param pSrc 압축 데이터
         * @param srcSize 압축 데이터 크기(바이트)
         * @param pDst 복원 결과를 쓸 버퍼
         * @param dstCapacity 대상 버퍼 용량(바이트)
         * @param outUncompressedSize 실제로 쓴 복원 데이터 크기(바이트)
         * @return 성공하면 true
         */
        virtual bool decompress( const void* pSrc,
                                 size_t      srcSize,
                                 void*       pDst,
                                 size_t      dstCapacity,
                                 size_t&     outUncompressedSize ) = 0;
    };
} // namespace sw
