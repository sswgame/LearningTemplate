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
        /** @brief 실제 영수증 제공자 설정 JSON 입니다(항목 형식은 `ReceiptProviderSet::readEntry`). 비우면 실제 제공자가 없습니다. 있으면 자격(키 · 서비스 계정)을 환경 변수에서 읽고, 읽지 못하면 기동 실패입니다. */
        PROPERTY()
        string _receiptProvidersFile{};
        /** @brief 제공자 `https://` 서버를 검증할 CA 번들 PEM 입니다. `_receiptProvidersFile` 이 https 주소를 쓰면 필수입니다. */
        PROPERTY()
        string _receiptTrustFile{};
    };
} // namespace sw
