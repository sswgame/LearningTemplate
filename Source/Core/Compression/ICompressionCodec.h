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
     *          팩은 자기 enum 을 이 enum 으로 **표 한 곳**에서 옮겨(`ResourcePakCompressionUtil::kArrCodecMapping`) 같은 레지스트리에서 코덱을 찾습니다.
     */
    enum class CompressionCodecType : uint8
    {
        None   = 0,   ///< 무압축(그대로 복사)
        RLE    = 1,   ///< 빠른 바이트 단위 RLE
        LZ4    = 2,   ///< LZ4. 해제가 가장 빠르다(로딩 시간이 곧 해제 시간인 곳)
        Zstd   = 3,   ///< Zstandard. 결과가 가장 작다(배포물)
        Zlib   = 4,   ///< Deflate. 리소스 팩의 기본 코덱이다(`PackConfig`)
        Custom = 255, ///< 사용자 정의 코덱
    };

    /**
     * @class ICompressionCodec
     * @brief 압축 · 해제 알고리즘의 인터페이스입니다.
     * @details 특정 압축 라이브러리(LZ4, Zstd 등)에 묶이지 않도록 인터페이스와 구현을 분리합니다.
     *
     *          **동시 호출 계약 — 한 인스턴스의 `compress` · `decompress` · `compressBound` · `decompressBound` 는 여러 스레드에서 동시에 불립니다.**
     *          레지스트리(`CompressionCodecRegistry`)가 코덱 종류마다 인스턴스 하나만 들고 있고, 리소스 팩 쿠커는 항목별 압축을 병렬로 돌리며
     *          (`ResourcePakWriter`) 팩 리더 · 네트워크 · 세이브도 같은 인스턴스를 씁니다. 그러므로 구현은 호출 사이에 바뀌는 멤버 · 정적 변수 ·
     *          재사용 버퍼를 두지 않고, 호출마다 문맥(zlib `z_stream`, zstd `CCtx`, LZ4 HC 상태)을 지역으로 만들어 호출 안에서 거둡니다.
     *          호출 횟수를 세는 테스트용 코덱 같은 것은 원자 값으로 셉니다. 상태가 꼭 필요한 압축기(학습 사전 등)는 이 인터페이스를 구현하지
     *          말고 스레드 한 개 전용 별도 클래스로 둡니다(`ZstdDictionaryCompressor` 가 그 예입니다).
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
         * @brief 압축 데이터 compressedSize 바이트가 풀려 나올 수 있는 최대 크기를 구합니다(포맷이 허락하는 최대 팽창률).
         * @details 해제하는 쪽은 스트림 헤더에 적힌 원본 크기를 그대로 할당 크기로 씁니다. 그 값을 할당 **전에** 이 상한과 비교해야
         *          몇 바이트짜리 망가진 입력이 수백 MB 를 잡고 초기화하게 만들지 못합니다(야간 퍼저 `Compression` 대상의 타임아웃:
         *          81 바이트 입력이 620 MB 를 요구했다). 팽창률은 포맷마다 달라서 코덱만 답할 수 있으므로 순수 가상입니다 —
         *          "모르면 무한대" 같은 기본 구현을 두면 그 코덱으로 적힌 스트림은 다시 헤더 값을 그대로 믿게 됩니다.
         *          compressedSize 는 메모리에 올라온 버퍼의 크기라 팽창률(최대 2^15)을 곱해도 64 비트를 넘지 않습니다.
         */
        virtual size_t decompressBound( size_t compressedSize ) const = 0;

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
