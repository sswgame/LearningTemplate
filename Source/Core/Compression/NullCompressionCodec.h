#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class NullCompressionCodec
     * @brief 무압축 코덱입니다.
     * @details 데이터를 바꾸지 않고 그대로 복사합니다. 디버깅용이나, 압축을 쓰지 않을 때의 폴백으로 씁니다.
     */
    class SW_API NullCompressionCodec final : public ICompressionCodec
    {
    public:
        NullCompressionCodec()                                             = default;
        virtual ~NullCompressionCodec() override                           = default;
        NullCompressionCodec( const NullCompressionCodec& )                = default;
        NullCompressionCodec& operator=( const NullCompressionCodec& )     = default;
        NullCompressionCodec( NullCompressionCodec&& ) noexcept            = default;
        NullCompressionCodec& operator=( NullCompressionCodec&& ) noexcept = default;

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
