/**
 * @file LocalSlotEnvelope.h
 * @brief 로컬 슬롯 봉투입니다. 모든 저장소가 같은 바이트 형식(형식 버전, 코덱, 봉인, 키 표시, CRC32/AEAD 태그)을 씁니다.
 * @details 리틀 엔디언, 머리 36 B:
 *          `[magic "SWLS" 4][봉투 판 u8 = 1][봉인 u8][코덱 u8][예약 u8][형식 판 u32][원래 크기 u64][몸 크기 u64][키 표시 u64]` 뒤에
 *          - None: `[몸][CRC32(머리 ‖ 몸) 4]`
 *          - Authenticated: `[본문][nonce 12][태그 16]`. 태그는 AEAD(빈 평문, 연관 데이터 = 헤더 ‖ 본문)다
 *          - Encrypted: `[nonce 12][암호문 = 본문 크기][태그 16]`. 연관 데이터는 헤더다
 *          압축 → 봉인 순서. 줄지 않으면 코덱 0(원문). 원래 크기 상한은 `ILocalStore::kMaxSlotSize`(압축 폭탄 막기).
 *          봉인 키와 키 표시는 장치 키에서 HKDF 로 파생한다. 키 표시가 다르면 WrongKey, 같은데 태그가 틀리면 Corrupt 다.
 *          장치 키는 그 PC 에 있으므로 치트는 막지 못한다. 실수와 가벼운 변조를 막는 용도다(경쟁 데이터의 원본은 서버).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CompressionCodecRegistry;
    class INetSecurityProvider;

    /** @brief 봉투를 만들고 열 때 쓰는 인터페이스들입니다(모두 빌려 쓴다). 봉인하지 않는 슬롯만 쓰면 암호 인터페이스와 키는 비워도 됩니다. */
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
     * @brief 봉투를 만들고 엽니다. 쓰는 인터페이스들이 스레드 안전하므로 아무 스레드에서나 부를 수 있습니다.
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
