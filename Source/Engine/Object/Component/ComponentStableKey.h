/**
 * @file ComponentStableKey.h
 * @brief 오브젝트 안에서 컴포넌트를 가리키는 안정 키 — `이름(없으면 타입)#같은 이름 중 몇 번째`.
 * @details 포인터도 id 도 씬을 다시 열면 바뀐다. 씬 파일의 부착 대상(`SceneComponent::_attachComponent`)과 에디터의
 *          선택 복원(핫리로드 · 씬 재로드 뒤)이 이 키로 컴포넌트를 되찾는다. 예전에는 둘이 같은 규칙을 각자 들었고
 *          형식도 달랐다(에디터는 첫 번째에 `#0` 을 붙이지 않았다) — 정본은 씬 파일에 저장되는 이쪽이다.
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
     * @brief 컴포넌트 안정 키의 생성과 해석. 같은 앞부분끼리만 센다 — 이름이 붙은 컴포넌트는 타입이 같아도 따로 센다.
     */
    class SW_API ComponentStableKey
    {
    public:
        /** @brief 컴포넌트의 키. 소유자가 없거나 nullptr 이면 빈 문자열입니다. */
        static string makeKey( const Component* pComp );

        /** @brief 키가 가리키는 컴포넌트. 형식이 아니거나(`#` 없음 · 숫자 아님) 없으면 nullptr 입니다. */
        static Component* findComponent( GameObject* pOwner, string_view key );

        /** @brief 키의 앞부분 — 컴포넌트 이름, 없으면 타입 이름(짧은 것 → 정규화된 것 → "Component"). */
        static string_view getBaseName( const Component* pComp );
    };
} // namespace sw
