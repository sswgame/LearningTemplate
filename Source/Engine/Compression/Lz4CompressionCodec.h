#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class Lz4CompressionCodec
     * @brief LZ4 압축 코덱 (외부 라이브러리 `lz4`)
     * @details 압축률보다 **속도**가 필요한 자리용입니다 — 런타임 로드 경로처럼 압축 해제 시간이
     *          곧 로딩 시간인 곳입니다. 압축률은 zlib/zstd 보다 낮습니다.
     * @note `compressionLevel` 을 0 보다 크게 주면 LZ4 HC(고압축) 경로로 갑니다. 압축만 느려지고
     *       **해제 속도는 같습니다** — 한 번 굽고 여러 번 읽는 데이터에 쓰기 좋습니다.
     */
    class SW_API Lz4CompressionCodec final : public ICompressionCodec
    {
    public:
        Lz4CompressionCodec()                                            = default;
        virtual ~Lz4CompressionCodec() override                          = default;
        Lz4CompressionCodec( const Lz4CompressionCodec& )                = default;
        Lz4CompressionCodec& operator=( const Lz4CompressionCodec& )     = default;
        Lz4CompressionCodec( Lz4CompressionCodec&& ) noexcept            = default;
        Lz4CompressionCodec& operator=( Lz4CompressionCodec&& ) noexcept = default;

        virtual CompressionCodecType getCodecType() const override;
        virtual const utf8*          getCodecName() const override;
        virtual size_t               compressBound( size_t uncompressedSize ) const override;

        virtual bool compress( const void* pSrc,
                               size_t      srcSize,
                               void*       pDst,
                               size_t      dstCapacity,
                               size_t&     outCompressedSize,
                               int32       compressionLevel = 0 ) override;

        virtual bool decompress( const void* pSrc,
                                 size_t      srcSize,
                                 void*       pDst,
                                 size_t      dstCapacity,
                                 size_t&     outUncompressedSize ) override;
    };
} // namespace sw
