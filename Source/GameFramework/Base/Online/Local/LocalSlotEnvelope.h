/**
 * @file LocalSlotEnvelope.h
 * @brief 로컬 슬롯 봉투 — 모든 저장소가 같은 바이트를 씁니다: 형식 판 · 코덱 · 봉인 · 키 표시 · CRC32/AEAD 태그.
 * @details 리틀 엔디언, 머리 36 B:
 *          `[magic "SWLS" 4][봉투 판 u8 = 1][봉인 u8][코덱 u8][예약 u8][형식 판 u32][원래 크기 u64][몸 크기 u64][키 표시 u64]` 뒤에
 *          - None: `[몸][CRC32(머리 ‖ 몸) 4]`
 *          - Authenticated: `[몸][nonce 12][태그 16]` — 태그는 AEAD(빈 평문, 연관 자료 = 머리 ‖ 몸)
 *          - Encrypted: `[nonce 12][암호문 = 몸 크기][태그 16]` — 연관 자료 = 머리
 *          압축 → 봉인 순서. 줄지 않으면 코덱 0(원문). 원래 크기 상한은 `ILocalStore::kMaxSlotSize`(압축 폭탄 막기).
 *          봉인 키는 장치 키에서 HKDF 로 가른다(봉인 키 · 키 표시) — 키 표시가 다르면 WrongKey, 같은데 태그가 틀리면 Corrupt.
 *          장치 키는 그 PC 에 있으므로 치트는 막지 못한다 — 실수 · 가벼운 변조 막기다(경쟁 데이터의 정본은 서버).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CompressionCodecRegistry;
    class INetSecurityProvider;

    /** @brief 봉투를 짓고 풀 때 쓰는 창구들입니다(모두 빌려 쓴다). 봉인하지 않는 슬롯만 쓰면 암호 창구 · 키는 비워도 된다. */
    struct LocalSealContext
    {
        INetSecurityProvider*     _pSecurityProvider{ nullptr };
        ILocalStoreKeyProvider*   _pKeyProvider{ nullptr };
        CompressionCodecRegistry* _pCodecRegistry{ nullptr }; ///< 비면 `CompressionCodecRegistry::getActive()`
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LocalSlotEnvelope
     * @brief 봉투 짓기 · 풀기입니다. 아무 스레드에서나(창구들이 스레드 안전).
     */
    struct SW_GF_API LocalSlotEnvelope
    {
        static constexpr uint8  kEnvelopeVersion = 1;
        static constexpr size_t kHeaderSize      = 36;

        static LocalStoreResult seal( const LocalSealContext& context, const uint8* pBody, size_t bodySize, const LocalStoreWriteOptions& options,
                                      vector<uint8>& outEnvelopeBytes );
        static LocalStoreResult open( const LocalSealContext& context, const uint8* pEnvelopeBytes, size_t envelopeByteCount, vector<uint8>& outBodyBytes,
                                      uint32& outFormatVersion );
    };
} // namespace sw
