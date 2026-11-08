/**
 * @file EngineCompressionCodecs.h
 * @brief Engine 이 외부 라이브러리로 구현한 압축 코덱들의 **등록 목록**을 한곳에 둡니다.
 */
#pragma once
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    class CompressionCodecRegistry;

    /**
     * @struct EngineCompressionCodecUtil
     * @brief `Engine/Compression` 이 제공하는 코덱을 레지스트리에 붙입니다.
     * @details Core 는 압축 라이브러리에 의존하지 않으므로(`ReflectionParser` 가 Core 를 링크합니다) LZ4 · Zstd · Zlib 은
     *          Engine 이 가져와 붙입니다. **그 목록을 호출부에 두지 않습니다** — 호출부가 손으로 고르면 빠진 코덱(예: `Zlib`)은
     *          클래스도 열거값도 있는데 `CompressionStream` 에서 쓸 수 없습니다(요청하면 무압축으로 떨어지고, 다른 도구가 쓴 스트림은
     *          읽히지 않습니다).
     */
    struct SW_API EngineCompressionCodecUtil
    {
        /** @brief Engine 이 제공하는 코덱 타입입니다. 코덱을 늘리면 여기와 `registerAll` 만 고칩니다. */
        static constexpr CompressionCodecType kArrCodecType[] = {
            CompressionCodecType::LZ4,
            CompressionCodecType::Zstd,
            CompressionCodecType::Zlib,
        };

        /** @brief 위 목록의 코덱을 모두 등록하고 그 구독들을 돌려줍니다(부르는 쪽이 레지스트리와 같은 수명으로 든다). 같은 타입이 이미 있으면 덮어씁니다. */
        [[nodiscard]] static vector<Subscription> registerAll( CompressionCodecRegistry& inoutRegistry );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LZ4CompressionCodec
     * @brief LZ4 압축 코덱 (외부 라이브러리 `lz4`)
     * @details 압축률보다 **속도**가 필요한 자리에 씁니다. 런타임 로드 경로처럼 압축 해제 시간이
     *          곧 로딩 시간인 곳입니다. 압축률은 zlib/zstd 보다 낮습니다.
     * @note `compressionLevel` 을 0 보다 크게 주면 LZ4 HC(고압축) 경로로 갑니다. 압축만 느려지고
     *       **해제 속도는 같습니다.** 한 번 쿠킹하고 여러 번 읽는 데이터에 쓰기 좋습니다.
     */
    class SW_API LZ4CompressionCodec final : public ICompressionCodec
    {
    public:
        LZ4CompressionCodec()                                            = default;
        virtual ~LZ4CompressionCodec() override                          = default;
        LZ4CompressionCodec( const LZ4CompressionCodec& )                = default;
        LZ4CompressionCodec& operator=( const LZ4CompressionCodec& )     = default;
        LZ4CompressionCodec( LZ4CompressionCodec&& ) noexcept            = default;
        LZ4CompressionCodec& operator=( LZ4CompressionCodec&& ) noexcept = default;

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

namespace sw
{
    /**
     * @class ZstdCompressionCodec
     * @brief Zstandard 압축 코덱 (외부 라이브러리 `zstd`)
     * @details 압축률과 속도의 균형이 필요한 자리에 씁니다. 배포 팩 · 세이브처럼 한 번 쿠킹하고 여러 번
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

namespace sw
{
    /**
     * @class ZlibCompressionCodec
     * @brief zlib(Deflate) 압축 코덱 (외부 라이브러리 `zlib`)
     * @details 리소스 팩의 기본 코덱입니다(`Config/Engine/PackConfig.json`). 팩 쿠커(`PackCooker`)와 리더가 같은 엔진 코덱을 쓰므로
     *          LZ4/Zstd 로 바꿔도 도구 설치가 필요 없습니다. 리더는 표준 zlib 이라 Deflate 전략에 제약이 없습니다.
     * @note 팩은 자기 디스크 enum(`PackCompressionType`)을 `PackCompressionUtil` 의 표로 `CompressionCodecType` 에 옮겨 레지스트리에서 이 코덱을
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
