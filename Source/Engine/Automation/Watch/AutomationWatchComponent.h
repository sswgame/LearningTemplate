/**
 * @file AutomationWatchComponent.h
 * @brief `AutomationWatchQuery::findTargetComponent` 의 정의입니다 — 컴포넌트를 찾는 감시 값 구현(.cpp)만 include 한다.
 * @details `AutomationWatch.h` 는 에디터 창(View)도 읽어 씬 헤더(GameObject)를 끌어올 수 없다(EditorLayers.toml panelViewBan).
 */
#pragma once
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    template <typename TComponent>
    const TComponent* AutomationWatchQuery::findTargetComponent( const utf8* pWhat, AutomationWatchValue& outValue ) const
    {
        const TComponent* pComponent = _pTargetObject != nullptr ? _pTargetObject->getComponent<TComponent>() : nullptr;
        if ( pComponent == nullptr )
            outValue.setText( describeMissing( pWhat ) );
        return pComponent;
    }
} // namespace sw
