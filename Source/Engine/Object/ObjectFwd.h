/**
 * @file ObjectFwd.h
 * @brief 오브젝트 모델 타입(`Component` · `GameObject` · `GameObjectManager` · `SceneComponent` · 틱 컨텍스트 · 핸들)의 이름만 알리는 전방 선언 헤더입니다.
 * @details 이 타입을 포인터 · 참조로만 받는 헤더가 정의 헤더(`Component.h` · `GameObject.h` 와 그 포함 사슬)를 끌어오지 않게 한다(언리얼의 `*Fwd.h` 와 같은 자리,
 *          docs/agents/CppStyle.md Includes and layers). 멤버를 부르거나 값으로 드는 파일은 정의 헤더를 직접 include 한다.
 */
#pragma once

namespace sw
{
    class Component;
    class SceneComponent;
    class GameObject;
    class GameObjectManager;
    class TickContext;

    class ComponentHandle;
    class GameObjectHandle;
    template <typename T>
    class TComponentHandle;
} // namespace sw
