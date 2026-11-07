/**
 * @file Engine/Presentation/WidgetVisibility.h
 * @brief 위젯의 보임과 히트 테스트 방식(`WidgetVisibility`)입니다. 게임의 뷰모델이 보임 칸으로 들고 HUD 문서가 `_visibility="{bind:…}"` 로 잇습니다.
 * @details 위젯 트리(`Engine/UI/WidgetTree/WidgetTypes.h`)가 아니라 표현 폴더에 둔다 — 뷰모델을 든 게임 코드는 UI 를 짓지 않는 전용 서버도 짓는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 보임과 히트 테스트 방식입니다(언리얼 ESlateVisibility 와 같은 다섯 가지). */
    ENUM()
    enum class WidgetVisibility : uint8
    {
        Visible,             ///< 보이고 클릭을 받습니다.
        Collapsed,           ///< 보이지 않고 공간도 차지하지 않습니다(레이아웃 크기 0).
        Hidden,              ///< 보이지 않지만 공간은 차지합니다.
        HitTestInvisible,    ///< 보이지만 자기와 자식 모두 클릭을 받지 않습니다(HUD 장식).
        SelfHitTestInvisible ///< 보이고 자기만 클릭을 받지 않으며 자식은 받습니다(겹친 패널).
    };
} // namespace sw
