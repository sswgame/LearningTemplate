#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class NullCompressionCodec
     * @brief 무압축(Pass-through) 코덱 구현체
     * @details 데이터를 변형하지 않고 원본 그대로 복사합니다. 디버깅 및 압축 미적용 폴백용입니다.
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
