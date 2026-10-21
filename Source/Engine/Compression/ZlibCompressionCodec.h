#pragma once
#include "Core/Compression/ICompressionCodec.h"

namespace sw
{
    /**
     * @class ZlibCompressionCodec
     * @brief zlib(Deflate) 압축 코덱 (외부 라이브러리 `zlib`)
     * @details 리소스 팩의 기본 코덱입니다(`Config/Engine/PackConfig.json`). 팩 쿠커(`PackCooker`)와 리더가 같은 엔진 코덱을 쓰므로
     *          LZ4/Zstd 로 바꿔도 도구 설치가 필요 없습니다. 리더는 표준 zlib 이라 Deflate 전략에 제약이 없습니다.
     * @note 팩은 자기 디스크 enum(`PackCompressionType`)을 `ResourcePakCompressionUtil` 의 표로 `CompressionCodecType` 에 옮겨 레지스트리에서 이 코덱을
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
        virtual size_t               decompressBound( size_t compressedSize ) const override;

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
