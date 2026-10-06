/**
 * @file NetCompression.h
 * @brief 네트워크 압축 봉투 `[u8 코덱 id][varuint 원래 크기][압축 바이트]` — 스트림 프레임(과 측정이 이기면 UDP 패킷)이 같이 씁니다.
 * @details 코덱은 Core `CompressionCodecRegistry` 의 **id 로만** 고른다(LZ4 · zstd 는 Engine 이 등록한다 — Core 네트워크는 라이브러리를 모른다).
 *          줄지 않으면 봉투를 쓰지 않는다(부르는 쪽이 원문을 보낸다). 받는 쪽은 원래 크기가 상한을 넘으면 **풀기 전에** 거절한다(압축 폭탄).
 *          순서는 압축 → 암호화(암호문은 줄지 않는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/vector.h"

namespace sw
{
    class CompressionCodecRegistry;
} // namespace sw

namespace sw
{
    /** @brief 압축 설정입니다. 기본은 꺼짐 — 켤지는 측정으로 정한다(`NetCompressionBenchTest`). */
    struct NetCompressionSettings
    {
        CompressionCodecType _codec{ CompressionCodecType::None }; ///< None = 끈다
        int32                _level{ 0 };                          ///< 코덱 레벨(0 = 코덱 기본)
        int32                _minInputBytes{ 128 };                ///< 이보다 작은 몸은 압축하지 않는다
    };
} // namespace sw

namespace sw
{
    struct SW_API NetCompressionUtil
    {
        static constexpr int32 kMaxEnvelopeHeaderSize = 1 + 5; ///< 코덱 표식 + varuint(32 비트)

        /**
         * @brief 압축해 @p outBytes 에 봉투를 씁니다. 줄었으면 true, 코덱이 없거나 줄지 않으면 false(@p outBytes 는 비운다 — 부르는 쪽이 원문을 보낸다).
         * @param pRegistry nullptr 이면 활성 등록부(`CompressionCodecRegistry::getActive`).
         */
        [[nodiscard]] static bool compressEnvelope( const NetCompressionSettings& settings, const uint8* pData, int32 size, vector<uint8>& outBytes,
                                                    CompressionCodecRegistry* pRegistry = nullptr );
        /**
         * @brief 봉투를 풉니다. 원래 크기가 @p maxRawSize 를 넘으면 **풀기 전에** false(압축 폭탄), 코덱이 없거나 깨졌거나 푼 크기가 다르면 false.
         * @param pRegistry nullptr 이면 활성 등록부.
         */
        [[nodiscard]] static bool decompressEnvelope( const uint8* pData, int32 size, int32 maxRawSize, vector<uint8>& outBytes,
                                                      CompressionCodecRegistry* pRegistry = nullptr );
    };
} // namespace sw
