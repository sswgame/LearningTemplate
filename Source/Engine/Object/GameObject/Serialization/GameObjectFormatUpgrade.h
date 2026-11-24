/**
 * @file GameObjectFormatUpgrade.h
 * @brief 오브젝트 칸을 담는 XML 에셋(씬 · 프리팹)이 함께 쓰는 판 올림 단계입니다. 단계 등록은 각 종류의 .cpp 가 한다(`SW_ASSET_FORMAT_UPGRADE`).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"

namespace sw
{
    /**
     * @struct GameObjectFormatUpgrade
     * @brief 씬 · 프리팹 문서의 오브젝트 칸 이름 바꾸기입니다. 데이터만 고치는 순수 함수다.
     */
    struct GameObjectFormatUpgrade
    {
        /**
         * @brief 문서 전체(엔티티 · 프리팹 오버라이드 · 중첩 오브젝트)의 `_siblingIndex` 속성 이름을 `_siblingOrder` 로 바꿉니다. 값은 그대로다 — 옛 0 부터 센 자리도 유효한 정렬 키다(설계 E-6).
         * @details 씬 판 2 → 3, 프리팹 판 0 → 1 단계다. 바꿀 속성이 없어도 true 다(기본값이라 적히지 않은 문서가 대부분이다).
         */
        static bool renameSiblingIndex( FormatUpgradePayload& payload );
    };
} // namespace sw
