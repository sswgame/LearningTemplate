/**
 * @file ComponentStableKey.h
 * @brief 오브젝트 안에서 컴포넌트를 가리키는 안정 키입니다. 형식은 고정 지역 id 의 `#<16자리 16진>` 입니다(감사 A-2).
 * @details 포인터도 런타임 id 도 씬을 다시 열면 바뀝니다. 씬 파일의 부착 대상(`SceneComponent::_attachComponent`), 프리팹 덮어쓰기(`PrefabOverrides`),
 *          세이브의 컴포넌트 핸들 · 정체(`ObjectStateSerializer`), 에디터 선택 복원이 이 키로 컴포넌트를 되찾습니다. 규칙은 여기 하나입니다.
 *          예전 키 `이름#n`(같은 이름 가운데 몇 번째)은 순번이라 앞 형제를 지우거나 끼우면 덮어쓰기 · 쌓인 세이브가 경고 없이 다른 대상으로 갔다 —
 *          유니티 fileID, 언리얼 SCS 노드의 변수 GUID, Godot 의 고유 노드 이름처럼 저작 때 정한 id 로 바꿨다.
 *          옮기는 동안: 옛 키도 읽는다(`findComponent` · `matchesKey`), id 가 없는 컴포넌트(옛 파일)의 id 는 옛 키에서 정한다(`resolveLocalIDs`) —
 *          그래서 id 를 아직 적지 않은 파일도 다시 읽을 때 같은 id 가 나온다. 구조를 바꾸기 전(지우기 · 순서 바꾸기 · 이름 바꾸기)에 굳힌다(`freezeLocalIDs`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class Component;
    class GameObject;

    /** @brief 한 오브젝트의 컴포넌트 하나 — 이름(없으면 타입)과 적힌 지역 id(0 이면 없음). `resolveLocalIDs` 가 `_resolved` 를 채운다. */
    struct ComponentLocalIDSlot
    {
        string _baseName;
        uint64 _storedID{ 0 };
        uint64 _resolved{ 0 };
    };

    /**
     * @class ComponentStableKey
     * @brief 컴포넌트 안정 키(`#<16진>`)를 만들고 해석합니다. 파일 머리말 참고.
     */
    class SW_API ComponentStableKey
    {
    public:
        /** @brief 지역 id 키의 머리 글자입니다 — 옛 키(`이름#n`)는 `#` 로 시작하지 않는다. */
        static constexpr utf8 kLocalIDPrefix = '#';

        /**
         * @brief 한 오브젝트의 컴포넌트 목록(@p inoutListSlot, 오브젝트 순서)의 지역 id 를 정합니다. 적힌 id 는 그대로, 없는 것은 옛 키 `이름#n` 의 64 비트 해시입니다.
         * @details 옛 키 해시가 적힌 id 와 겹치거나 0 이면 같은 키로 다시 섞는다(결정적 — 같은 목록은 늘 같은 답). 같은 목록을 C++ 오브젝트(`getLocalID`)와
         *          XML 글(`PrefabOverrides`)이 같은 규칙으로 푼다.
         */
        static void resolveLocalIDs( vector<ComponentLocalIDSlot>& inoutListSlot );

        /** @brief 컴포넌트의 지역 id 입니다(적힌 것, 없으면 `resolveLocalIDs` 의 값). 소유자가 없거나 nullptr 이면 0 입니다. */
        static uint64 getLocalID( const Component* pComp );
        /**
         * @brief 오브젝트의 id 없는 컴포넌트마다 지금 정해진 id 를 적어 굳힙니다. 형제 구조를 바꾸기 전에 부른다 — 굳히지 않으면 옛 키 순번이 바뀌어 id 가 따라 바뀐다.
         * @details `GameObject::removeComponent` · `moveComponent` 와 에디터 이름 바꾸기가 부른다. 로드 중에는 부르지 않는다(뒤 형제의 적힌 id 를 아직 읽지 않았다).
         */
        static void freezeLocalIDs( GameObject& owner );
        /** @brief 복사한 컴포넌트(@p pComp)에 오브젝트 안에서 겹치지 않는 새 id 를 줍니다 — 같은 오브젝트에 상태를 베껴 붙이면 원본의 id 가 따라온다. */
        static void assignFreshLocalID( Component* pComp );
        /** @brief 운영체제 난수 64 비트 지역 id(0 이 아님)입니다 — 겹침 검사는 부르는 쪽이 한다(중첩 프리팹 노드의 id 도 이것으로 만든다). */
        static uint64 makeRandomLocalID();
        /** @brief 값을 베껴 덮어쓴 컴포넌트(@p pComp — 에디터 값 붙여 넣기)의 지역 id 를 덮기 전 값 @p localID 로 되돌립니다. 베낀 글에 원본의 id 가 들어 있다. */
        static void restoreLocalID( Component* pComp, uint64 localID );

        /** @brief 컴포넌트의 키(`#<16진>`)입니다. 소유자가 없거나 nullptr 이면 빈 문자열입니다. */
        static string makeKey( const Component* pComp );
        /** @brief 지역 id @p localID 의 키입니다. */
        static string formatKey( uint64 localID );
        /** @brief 키 @p key 의 지역 id 입니다. 지역 id 키가 아니면(옛 키 · 틀린 글) 없음입니다. */
        static optional<uint64> parseKey( string_view key );
        /** @brief 옛 키(`이름#n`)입니다 — 옮기는 동안 옛 세이브의 키 해시 · 정체를 맞춰 볼 때만 쓴다. */
        static string makeLegacyKey( const Component* pComp );
        /** @brief @p key 가 @p pComp 를 가리키면 true 입니다 — 지역 id 키, 또는 옮기는 동안 옛 키. */
        static bool matchesKey( const Component* pComp, string_view key );

        /** @brief 키가 가리키는 컴포넌트입니다. 지역 id 키 또는 옛 키(`이름#n`)를 받는다. 형식이 아니거나 없으면 nullptr 입니다. */
        static Component* findComponent( GameObject* pOwner, string_view key );

        /** @brief 키의 이름 부분입니다. 컴포넌트 이름, 비어 있으면 타입 이름(짧은 것 → 정규화된 것 → "Component")입니다. */
        static string_view getBaseName( const Component* pComp );
    };
} // namespace sw
