#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class ZlibCompressionCodec
     * @brief zlib(Deflate) 압축 코덱 (외부 라이브러리 `zlib`)
     * @details 리소스 팩이 쓰는 코덱입니다. 팩을 쿠킹하는 쪽이 파이썬(`Scripts/generate/CookAssets.py`)이고
     *          파이썬 표준 라이브러리에 `zlib` 이 들어 있기 때문입니다. LZ4/Zstd 는 표준이 아니라
     *          쿠킹하는 쪽에 추가 의존성이 필요합니다.
     * @note 팩은 자기 디스크 enum(`PackCompressionType`)을 `PackCompressionUtil` 의 표로 `CompressionCodecType` 에 옮겨 등록부에서 이 코덱을
     *       찾습니다. 두 디스크 번호는 서로 다르고 그대로입니다.
     */
    class SW_API ZlibCompressionCodec final : public ICompressionCodec
    {
    public:
        ZlibCompressionCodec()                                             = default;
        virtual ~ZlibCompressionCodec() override                           = default;
        ZlibCompressionCodec( const ZlibCompressionCodec& )                = default;
        ZlibCompressionCodec& operator=( const ZlibCompressionCodec& )     = default;
        ZlibCompressionCodec( ZlibCompressionCodec&& ) noexcept            = default;
        ZlibCompressionCodec& operator=( ZlibCompressionCodec&& ) noexcept = default;

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
