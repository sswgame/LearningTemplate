/**
 * @file EconomyKitConfig.h
 * @brief 경제 키트의 서버 설정입니다(`_listKit[]._settings`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 지급한 뒤 스토어 환불이 온 거래를 얼마나 회수하는지입니다(설정 `_refundRecovery`, 열거자 이름).
     * @details 애플 · 구글 플레이는 환불한 구매의 콘텐츠 회수를 개발자에게 맡기고 PlayFab 도 회수 규칙을 타이틀이 정한다 — 그래서 코드 기본값 없이 설정이 고른다.
     */
    ENUM()
    enum class EconomyRefundRecoveryMode : uint8
    {
        Debt = 0, ///< 지급한 만큼 모두 회수한다. 이미 쓴 몫은 빚(음수 잔액)이 되고 빚이 있는 재화로는 살 수 없다
        Unspent,  ///< 남은 잔액 안에서만 회수한다. 빚을 만들지 않는다(이미 쓴 몫은 운영이 따로 처리한다)
        Count     ///< 정하지 않음 — 서비스가 기동을 거절한다
    };
} // namespace sw

namespace sw
{
    /** @brief 경제 키트의 서버 설정입니다. 파일 경로는 프로젝트 루트 기준이고, 기동 때 절대 경로로 풀며 못 풀면 기동 실패입니다. */
    REFLECT()
    struct EconomyKitConfig
    {
        REFLECT_BODY();
        /** @brief 화폐 카탈로그입니다(리소스 경로, `<CurrencyCatalog>`). */
        PROPERTY( Required )
        string _currencyCatalog{};
        /** @brief 상품 카탈로그입니다(리소스 경로, `<OfferCatalog>`). */
        PROPERTY( Required )
        string _offerCatalog{};
        /** @brief 가짜 영수증 제공자("fake")를 등록합니다(DevOnline 단위만 — 확장점 `EconomyDevOptions`, 그 단위가 없는 구성은 기동 실패). */
        PROPERTY()
        bool _bFakeReceipts{ false };
        /** @brief 스토어의 샌드박스 영수증을 받습니다(스테이징 서버). */
        PROPERTY()
        bool _bAcceptSandboxReceipts{ false };
        /** @brief 지급 뒤 온 스토어 환불의 회수 정책입니다(`Debt` · `Unspent`). */
        PROPERTY( Required )
        EconomyRefundRecoveryMode _refundRecovery{ EconomyRefundRecoveryMode::Count };
        /** @brief 실제 영수증 제공자 설정 JSON 입니다(항목 형식은 `ReceiptProviderSet::readEntry`). 비우면 실제 제공자가 없습니다. 있으면 자격(키 · 서비스 계정)을 환경 변수에서 읽고, 읽지 못하면 기동 실패입니다. */
        PROPERTY()
        string _receiptProvidersFile{};
        /** @brief 제공자 `https://` 서버를 검증할 CA 번들 PEM 입니다. `_receiptProvidersFile` 이 https 주소를 쓰면 필수입니다. */
        PROPERTY()
        string _receiptTrustFile{};
    };
} // namespace sw
