/**
 * @file EconomyDevOptions.h
 * @brief 경제 키트(서버)의 개발 대체물 확장점입니다 — 가짜 영수증 검증 제공자(`fakeReceipts`).
 * @details DevOnline 단위(`EconomyDevOnline.cpp`)가 올리고(`DevOnlineExtension.h`), 확장점은 `EconomyService.cpp` 가 정의한다. 단위가 없는 구성(배포)에서
 *          `fakeReceipts` 를 켜면 키트 기동 오류이고, 경제 서비스는 개발 전용 제공자(`IReceiptValidator::isDevelopmentOnly`)를 가진 레지스트리를 거절한다.
 *          같은 모듈(GF_Server_Economy) 안에서만 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IReceiptValidator;

    template <typename TValue>
    class DevOnlineExtension;

    /**
     * @struct EconomyDevOptions
     * @brief 경제 키트의 개발 대체물 값입니다. 파일 머리말 참고.
     */
    struct EconomyDevOptions
    {
        using CreateFakeReceiptValidatorFunction = unique_ptr<IReceiptValidator> ( * )();

        CreateFakeReceiptValidatorFunction _pCreateFakeReceiptValidator{ nullptr }; ///< `fakeReceipts` 가 레지스트리에 올리는 가짜 스토어("fake")를 만든다

        static DevOnlineExtension<EconomyDevOptions> s_devOnline;
    };
} // namespace sw
