/**
 * @file GameObjectHandle.h
 * @brief 게임 오브젝트 핸들입니다. 프레임을 넘겨 들고 있을 때는 핸들을, 지금 바로 쓸 때만 `GameObject*` 를 씁니다.
 *
 * @note `ComponentHandle` 과 같은 곳(엔진 바닥 `Engine/Foundation`)에 둡니다. 게임 오브젝트 개념이라 Core 의 것이 아니고(언리얼 `FObjectHandle` 도 CoreUObject 다 — 기억),
 *       정수 하나(objectID 와 핸들 세대)뿐인 값 타입이라 Core 만 include 합니다.
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @brief GameObject 를 objectID 와 핸들 세대로 가리키는 값 타입 핸들입니다.
     * @details 오브젝트·컴포넌트를 가리키는 방법은 둘뿐입니다.
     *          - `GameObject*` / `Component*` — **빌리기**. 지금 부른 함수 안에서, 길어야 이번 프레임 안에서만 씁니다.
     *          - `GameObjectHandle` / `ComponentHandle` — **보관**. 프레임을 넘겨 들고 있을 때 씁니다. 쓸 때마다
     *            `GameObjectManager::resolveGameObject` / `resolveComponent` 로 풀고, 대상이 파괴됐으면 nullptr 을 받습니다.
     *
     *          objectID 는 매니저가 단조 증가로 발급하고 다시 쓰지 않으므로, 파괴된 대상의 핸들이 다른 오브젝트로
     *          풀리는 일이 없습니다. 이름을 바꿔도 끊기지 않습니다. 에디터 되돌리기 · 플레이 세션 복원 · 핫 리로드는
     *          오브젝트를 다시 만들 때 같은 objectID 와 componentID 를 복원하므로, 그 너머로도 핸들이 이어집니다.
     *
     *          풀이 돌려받은 오브젝트는 파괴되지 않아 objectID 가 같습니다. 그래서 풀은 돌려받을 때 오브젝트의 핸들 세대를 올립니다
     *          (`GameObject::invalidateHandles`) — 앞 생을 가리키던 핸들은 파괴된 대상처럼 nullptr 로 풀리고, 다시 꺼낸 생을 보지 못합니다
     *          (유니티 DOTS `Entity` 의 Index · Version, 언리얼 `FWeakObjectPtr` 의 일련번호 자리).
     *
     *          objectID 는 **매니저마다 따로 셉니다.** 그래서 핸들은 자기를 만든 매니저 안에서만 뜻이 있습니다.
     *
     * @note 이름으로 찾는 참조를 두지 말 것 — 이름을 바꾸면 끊기고, 옛 이름으로 새 오브젝트가 생기면 조용히 그쪽을 가리키며,
     *       타입으로 찾으면 같은 타입 컴포넌트가 둘일 때 첫 번째를 잡습니다.
     */
    class GameObjectHandle
    {
    public:
        /** @brief 무효 핸들을 만듭니다. */
        constexpr GameObjectHandle() noexcept = default;

        /** @brief objectID 를 담는 비트 수입니다. 나머지 위 16 비트가 핸들 세대입니다. */
        static constexpr uint32 kObjectIDBits = 48;
        /** @brief 담을 수 있는 가장 큰 objectID 입니다. */
        static constexpr uint64 kMaxObjectID = ( uint64{ 1 } << kObjectIDBits ) - 1;

        /** @brief 오브젝트의 id 와 지금 핸들 세대로 핸들을 만듭니다. 오브젝트가 있으면 `GameObject::getHandle` 을 부르십시오 — id 만 알면 그 생을 모릅니다. */
        static constexpr GameObjectHandle make( uint64 objectID, uint16 generation ) noexcept { return fromValue( objectID | ( static_cast<uint64>( generation ) << kObjectIDBits ) ); }
        /** @brief 담긴 값 그대로의 핸들입니다 — `value()` 를 되돌릴 때와, 로드가 풀기 전에 파일의 저장 id(64비트)를 잠시 담을 때만 씁니다. 0 은 무효입니다. */
        static constexpr GameObjectHandle fromValue( uint64 value ) noexcept
        {
            GameObjectHandle handle;
            handle._value = value;
            return handle;
        }

        /** @brief 담긴 값 그대로입니다. 핸들을 모르는 자료(번호만 든 모델 · 아카이브 · 텍스트)에 같은 생을 가리키는 키로 싣습니다. */
        [[nodiscard]] constexpr uint64 value() const noexcept { return _value; }
        /** @brief 가리키는 오브젝트의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 objectID() const noexcept { return _value & kMaxObjectID; }
        /** @brief 만들 때의 핸들 세대입니다. 오브젝트의 지금 세대와 다르면 앞 생을 가리킵니다. */
        [[nodiscard]] constexpr uint16 generation() const noexcept { return static_cast<uint16>( _value >> kObjectIDBits ); }
        /** @brief 무효 핸들이 아니면 true 입니다. 대상이 아직 살아 있는지는 매니저로 조회해 봐야 압니다. */
        [[nodiscard]] constexpr bool     isValid() const noexcept { return _value != 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( GameObjectHandle lhs, GameObjectHandle rhs ) noexcept { return lhs._value == rhs._value; }
        friend constexpr bool operator!=( GameObjectHandle lhs, GameObjectHandle rhs ) noexcept { return ( lhs == rhs ) == false; }
        /** @brief objectID 순입니다(같은 오브젝트의 생은 세대 순) — 결정론 순서가 풀 재사용 횟수를 따르지 않는다. */
        friend constexpr bool operator<( GameObjectHandle lhs, GameObjectHandle rhs ) noexcept
        {
            return lhs.objectID() != rhs.objectID() ? lhs.objectID() < rhs.objectID() : lhs._value < rhs._value;
        }

    private:
        uint64 _value{ 0 };
    };
} // namespace sw

template <>
struct std::hash<sw::GameObjectHandle>
{
    size_t operator()( sw::GameObjectHandle handle ) const noexcept { return std::hash<uint64>{}( handle.value() ); }
};
