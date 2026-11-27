/**
 * @file AppearanceSlotRequest.h
 * @brief 외형 해석에 넘기는 슬롯 하나의 요청(낀 아이템 · 인스턴스 상태 · 보이는 외형 덮어쓰기)입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Math/Radians.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/formatString.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/CustomizationValueSet.h"

namespace sw
{
    /**
     * @brief 슬롯 하나의 요청입니다. 낀 아이템과 그 인스턴스 상태, 보이는 외형 덮어쓰기(형상 변경), 외형 상태를 담으며 해석기의 입력으로 쓰입니다.
     * @details `_damage` 는 맞은 피해와 닳은 내구도 중 큰 쪽(0..1)이다. `CharacterAppearanceSpec::applyEquipment` 가 채운다.
     */
    struct AppearanceSlotRequest
    {
        CustomizationValueSet _customization{};    ///< 아이템 인스턴스의 꾸미기 값
        vector<hashed_string> _listDetachedPart{}; ///< 떨어져 나간 부품
        hashed_string         _slot{};
        hashed_string         _itemID{};                    ///< 비어 있으면 빈 슬롯
        hashed_string         _visibleVisual{};             ///< 형상 변경. 비어 있으면 아이템 자신의 외형
        hashed_string         _state{};                     ///< 외형 상태(뽑음, 꽂음, 켬, 끔). 비어 있으면 외형의 기본 상태
        float32               _damage{ 0.0f };              ///< 외형 피해 0..1
        uint8                 _bSuppressed{ SW_FALSE };     ///< 장착 조건이 깨져 숨겨진 상태
        uint8                 _bHiddenByPlayer{ SW_FALSE }; ///< 플레이어가 숨긴 슬롯(투구 숨기기). 장비 효과는 그대로이고 외형 · 몸 채널만 빠진다. 선택 · 공유 코드(판 2)에 실린다
    };
} // namespace sw
