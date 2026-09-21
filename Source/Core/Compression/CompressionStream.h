/**
 * @file CompressionStream.h
 * @brief 압축 바이너리 컨테이너 — 헤더 · 체크섬 · 코덱 디스패치.
 */
#pragma once
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/vector.h"

namespace sw
{
    class CompressionCodecRegistry;

    /**
     * @struct CompressionHeader
     * @brief 압축 바이너리 컨테이너 헤더 (28 바이트)
     * @details **이 구조체는 그대로 디스크에 기록된다.** 필드를 더하거나 크기를 바꾸면 예전에 쓴
     *          스트림을 못 읽는다 — 아래 `static_assert` 가 크기를 못박아 두었으니, 포맷을 정말로
     *          바꿀 때만 `_version` 과 함께 고친다.
     */
#pragma pack( push, 1 )
    struct SW_API CompressionHeader
    {
        /** @brief 'SWCS' (SW Compression Stream) — 스트림 선두 4바이트. */
        static constexpr uint32 kMagic = 0x53574353;
        /** @brief 현재 컨테이너 포맷 판. 읽을 때 이 값만 받아들인다. */
        static constexpr uint8 kVersion = 1;
        /** @brief `_flags` 비트: 페이로드 뒤에 FNV-1a 체크섬 검증을 요구한다. */
        static constexpr uint16 kFlagChecksum = 0x01;

        uint32               _magic{ kMagic };
        uint8                _version{ kVersion };
        CompressionCodecType _codecType{ CompressionCodecType::None }; ///< 1바이트 — enum 의 언더라잉 타입이 uint8 이다
        uint16               _flags{ 0 };                              ///< kFlagChecksum 조합
        uint64               _uncompressedSize{ 0 };
        uint64               _compressedSize{ 0 };
        uint32               _checksum{ 0 }; ///< FNV-1a 체크섬 (무결성 검증용)
    };
#pragma pack( pop )

    // 디스크 포맷이다 — 크기가 바뀌면 예전 스트림이 조용히 어긋난다. 주석이 아니라 컴파일러가 지킨다.
    static_assert( sizeof( CompressionHeader ) == 28, "CompressionHeader 는 디스크 포맷입니다 — 28바이트가 아니면 예전 스트림을 못 읽습니다" );

    /**
     * @class CompressionStream
     * @brief 바이너리 스트림 압축 및 역압축 헬퍼
     * @details 헤더 캡슐화, 무결성 체크섬 검증, 자동 코덱 디스패칭을 수행합니다.
     */
    class SW_API CompressionStream
    {
    public:
        /** @brief 스트림 매직. 정본은 `CompressionHeader::kMagic` 이고 여기서는 그것을 가리킨다. */
        static constexpr uint32 kMagicNumber = CompressionHeader::kMagic;

        /**
         * @brief 스트림 하나가 풀어 놓을 수 있는 최대 바이트 수 (1 GiB).
         * @details 헤더의 `_uncompressedSize` 는 **곧바로 할당 크기가 된다.** 망가진(또는 악의적인)
         *          헤더가 2^60 을 적어 두면 코덱이 한 바이트도 읽기 전에 그 resize 에서 메모리가
         *          터진다 — 압축 파일 몇 바이트로 프로세스를 죽일 수 있다는 뜻이다.
         *          코덱마다 팽창률이 달라(RLE 65배, Deflate 는 1000배가 넘는다) 압축 크기로부터
         *          정확한 상한을 낼 수는 없으므로, **이 컨테이너가 다루는 가장 큰 것**(씬·세이브·
         *          리소스 팩 조각)보다 넉넉히 크고 기계를 재우기에는 충분히 작은 값을 못박는다.
         *          여기에 걸린다면 압축 스트림이 아니라 그 데이터가 이 컨테이너에 맞지 않는 것이다.
         */
        static constexpr uint64 kMaxUncompressedSize = uint64{ 1 } * 1024 * 1024 * 1024;

        /**
         * @brief 메모리 버퍼를 압축하여 헤더가 포함된 압축 바이너리 스트림으로 생성합니다.
         */
        static bool compressBuffer( const void*                     pSrc,
                                    size_t                          srcSize,
                                    vector<uint8>&                  outBytes,
                                    CompressionCodecType            codecType        = CompressionCodecType::RLE,
                                    int32                           compressionLevel = 0,
                                    const CompressionCodecRegistry* pRegistry        = nullptr );

        /**
         * @brief 압축 바이너리 스트림을 역압축하여 원본 바이트 버퍼를 복원합니다.
         */
        static bool decompressBuffer( const void*                     pSrc,
                                      size_t                          srcSize,
                                      vector<uint8>&                  outBytes,
                                      const CompressionCodecRegistry* pRegistry = nullptr );

        /**
         * @brief 고정 크기 출력 버퍼에 압축 데이터를 복원합니다.
         */
        static bool decompressBuffer( const void*                     pSrc,
                                      size_t                          srcSize,
                                      void*                           pDst,
                                      size_t                          dstCapacity,
                                      size_t&                         outUncompressedSize,
                                      const CompressionCodecRegistry* pRegistry = nullptr );

        /**
         * @brief 압축 스트림 헤더를 검증하고 메타데이터를 파싱합니다.
         */
        static bool verifyHeader( const void*        pData,
                                  size_t             dataSize,
                                  CompressionHeader& outHeader );

        /**
         * @brief 버퍼의 FNV-1a 32비트 체크섬을 계산합니다.
         */
        static uint32 computeChecksum( const void* pData, size_t dataSize );
    };
} // namespace sw
