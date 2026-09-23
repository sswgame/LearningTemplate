#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class RleCompressionCodec
     * @brief 엔진에 내장된 빠른 RLE(Run-Length Encoding) 코덱입니다.
     * @details 외부 라이브러리 없이 동작하는 가벼운 바이트 단위 RLE 입니다. 제어 바이트의 최상위 비트로 반복 런과 리터럴 런을 구분합니다.
     */
    class SW_API RleCompressionCodec final : public ICompressionCodec
    {
    public:
        RleCompressionCodec()                                            = default;
        virtual ~RleCompressionCodec() override                          = default;
        RleCompressionCodec( const RleCompressionCodec& )                = default;
        RleCompressionCodec& operator=( const RleCompressionCodec& )     = default;
        RleCompressionCodec( RleCompressionCodec&& ) noexcept            = default;
        RleCompressionCodec& operator=( RleCompressionCodec&& ) noexcept = default;

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
