/**
 * @file ComponentStableKey.h
 * @brief 오브젝트 안에서 컴포넌트를 가리키는 안정 키입니다. 형식은 `이름(없으면 타입)#같은 이름 중 몇 번째` 입니다.
 * @details 포인터도 id 도 씬을 다시 열면 바뀝니다. 씬 파일의 부착 대상(`SceneComponent::_attachComponent`)과 에디터의
 *          선택 복원(핫 리로드 · 씬 재로드 뒤)이 이 키로 컴포넌트를 되찾습니다. 예전에는 둘이 같은 규칙을 각자 들었고
 *          형식도 달랐습니다(에디터는 첫 번째에 `#0` 을 붙이지 않았습니다). 기준은 씬 파일에 저장되는 이쪽입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class Component;
    class GameObject;

    /**
     * @class ComponentStableKey
     * @brief 컴포넌트 안정 키(`타입이름#n`)를 만들고 해석합니다. 같은 타입끼리만 셉니다.
     * @details 이름표(`Component::setComponentName`)는 키에 들어가지 않습니다 — 저장되지 않으므로, 이름표로 만든 키는 씬을 다시 읽으면
     *          가리킬 곳이 없습니다. 예전에는 이름표를 단 컴포넌트를 그 이름으로 따로 셌습니다.
     */
    class SW_API ComponentStableKey
    {
    public:
        /** @brief 컴포넌트의 키입니다. 소유자가 없거나 nullptr 이면 빈 문자열입니다. */
        static string makeKey( const Component* pComp );

        /** @brief 키가 가리키는 컴포넌트입니다. 형식이 아니거나(`#` 없음 · 숫자 아님) 없으면 nullptr 입니다. */
        static Component* findComponent( GameObject* pOwner, string_view key );

        /** @brief 키의 앞부분입니다. 컴포넌트 이름, 없으면 타입 이름(짧은 것 → 정규화된 것 → "Component")입니다. */
        static string_view getBaseName( const Component* pComp );
    };
} // namespace sw
