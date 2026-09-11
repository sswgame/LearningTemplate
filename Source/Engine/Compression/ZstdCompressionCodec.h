#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class ZstdCompressionCodec
     * @brief Zstandard 압축 코덱 (외부 라이브러리 `zstd`)
     * @details 압축률과 속도의 균형이 필요한 자리용입니다 — 배포 팩·세이브처럼 한 번 굽고 여러 번
     *          읽는 데이터에 맞습니다. 같은 압축률이면 zlib 보다 해제가 빠릅니다.
     * @note `compressionLevel` 이 0 이면 zstd 기본 레벨(3)을 씁니다. 음수는 고속 레벨입니다.
     */
    class SW_API ZstdCompressionCodec final : public ICompressionCodec
    {
    public:
        ZstdCompressionCodec()                                             = default;
        virtual ~ZstdCompressionCodec() override                           = default;
        ZstdCompressionCodec( const ZstdCompressionCodec& )                = default;
        ZstdCompressionCodec& operator=( const ZstdCompressionCodec& )     = default;
        ZstdCompressionCodec( ZstdCompressionCodec&& ) noexcept            = default;
        ZstdCompressionCodec& operator=( ZstdCompressionCodec&& ) noexcept = default;

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
