/**
 * @file ComponentStableKey.h
 * @brief 오브젝트 안에서 컴포넌트를 가리키는 안정 키입니다. 형식은 `이름(없으면 타입)#같은 이름 중 몇 번째` 입니다.
 * @details 포인터도 id 도 씬을 다시 열면 바뀝니다. 씬 파일의 부착 대상(`SceneComponent::_attachComponent`)과 에디터의
 *          선택 복원(핫 리로드 · 씬 재로드 뒤)이 이 키로 컴포넌트를 되찾습니다. 규칙은 여기 하나이며, 기준은 씬 파일에 저장되는
 *          형식입니다(첫 번째에도 `#0` 을 붙인다).
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
     * @brief 컴포넌트 안정 키(`이름표#n`)를 만들고 해석합니다. 같은 이름표끼리만 셉니다.
     * @details 이름표(`Component::setComponentName`, 기본은 타입 이름)는 상태와 함께 저장되므로 씬을 다시 읽어도 같은 키가 같은 컴포넌트를
     *          가리킵니다 — 언리얼이 컴포넌트 이름으로 부착 대상 · 오버라이드를 가리키는 것과 같습니다. 이름표가 없는 상태는 타입 이름으로
     *          읽히므로 `타입이름#n` 키가 그대로 맞습니다.
     */
    class SW_API ComponentStableKey
    {
    public:
        /** @brief 컴포넌트의 키입니다. 소유자가 없거나 nullptr 이면 빈 문자열입니다. */
        static string makeKey( const Component* pComp );

        /** @brief 키가 가리키는 컴포넌트입니다. 형식이 아니거나(`#` 없음 · 숫자 아님) 없으면 nullptr 입니다. */
        static Component* findComponent( GameObject* pOwner, string_view key );

        /** @brief 키의 앞부분입니다. 컴포넌트 이름표, 비어 있으면 타입 이름(짧은 것 → 정규화된 것 → "Component")입니다. */
        static string_view getBaseName( const Component* pComp );
    };
} // namespace sw
