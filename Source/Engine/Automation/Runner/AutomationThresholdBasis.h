/**
 * @file AutomationThresholdBasis.h
 * @brief 이미지 단언(`<ExpectImage>`) 문턱의 근거를 정의합니다. 문턱이 잰 값인지 아직 재지 않은 어림값인지 속성 형식으로 갈립니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 문턱의 근거 종류입니다. */
    enum class AutomationThresholdBasisKind : uint8
    {
        Measured,    ///< 잰 값이 근거입니다. `measured="2026-10-22 dx12:0.095 vk:0.095"` — 날짜와 백엔드별 잰 값이 있어야 읽힙니다.
        Provisional, ///< 아직 재지 않은 어림값입니다. `provisional="이유"` — 시나리오가 통과해도 종료 코드는 `AutomationOutcome::Unconfirmed` 이고 잰 값을 `measured` 로 옮기기 전까지 미확정으로 보고합니다.
    };

    /**
     * @struct AutomationThresholdBasis
     * @brief 문턱 단언 하나의 근거입니다. `measured` 와 `provisional` 속성 가운데 정확히 하나를 읽은 값입니다.
     * @details 문턱 숫자를 측정 전에 데이터에 적는 흐름이 있어도, 그 숫자가 어림값이라는 사실이 단언 옆에 형식으로 남게 한다(적지 않으면 읽기 오류).
     *          `measured` 글은 `YYYY-MM-DD <백엔드>:<값> …` 이다. 백엔드는 명령줄 이름(`dx11` · `dx12` · `vk` · `gl`)이고, 모든 백엔드가 같은 값이면 `all:<값>` 하나로 적는다.
     */
    struct SW_API AutomationThresholdBasis
    {
        AutomationThresholdBasisKind _kind{ AutomationThresholdBasisKind::Measured };
        string                       _text{}; ///< 속성에 적힌 글(잰 날짜와 값, 또는 어림값인 이유)

        /**
         * @brief `measured` · `provisional` 속성 글(없으면 nullptr)에서 근거를 읽습니다. 정확히 하나여야 하고, 둘 다 형식에 맞아야 합니다.
         * @param pMeasured `measured` 속성 글 또는 nullptr
         * @param pProvisional `provisional` 속성 글 또는 nullptr
         * @param outError 틀리면 이유(단계 위치 접두어는 부르는 쪽이 붙인다)
         */
        [[nodiscard]] static bool tryParse( const string* pMeasured, const string* pProvisional, AutomationThresholdBasis& outBasis, string& outError );
    };
} // namespace sw
