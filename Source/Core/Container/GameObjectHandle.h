/**
 * @file GameObjectHandle.h
 * @brief 게임 오브젝트 핸들. 프레임을 넘겨 들고 있을 때는 핸들을, 지금 당장 쓸 때만 `GameObject*` 를 씁니다.
 *
 * @note `ComponentHandle` 과 같은 자리(`Core/Container`)에 둡니다. objectId 정수 하나뿐인 값 타입이라 Core 밖을 모릅니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief GameObject 를 objectId 로 가리키는 값 타입 핸들입니다.
     * @details 오브젝트·컴포넌트를 가리키는 방법은 둘뿐입니다.
     *          - `GameObject*` / `Component*` — **빌리기**. 지금 부른 함수 안에서, 길어야 이번 프레임 안에서만 씁니다.
     *          - `GameObjectHandle` / `ComponentHandle` — **보관**. 프레임을 넘겨 들고 있을 때 씁니다. 쓸 때마다
     *            `GameObjectManager::resolveGameObject` / `resolveComponent` 로 풀고, 대상이 파괴됐으면 nullptr 을 받습니다.
     *
     *          objectId 는 매니저가 단조 증가로 발급하고 다시 쓰지 않으므로, 파괴된 대상의 핸들이 다른 오브젝트로
     *          풀리는 일이 없습니다. 이름을 바꿔도 끊기지 않습니다. 에디터 되돌리기 · 플레이 세션 복원 · 핫 리로드는
     *          오브젝트를 다시 만들 때 같은 objectId 와 componentId 를 되살리므로, 그 너머로도 핸들이 이어집니다.
     *
     *          objectId 는 **매니저마다 따로 셉니다.** 그래서 핸들은 자기를 만든 매니저 안에서만 뜻이 있습니다.
     *
     * @note 예전에는 이름으로 찾는 `GameObjectPtr` · `ComponentPtr` 도 있었습니다. 이름을 바꾸면 끊기고, 옛 이름으로 새
     *       오브젝트가 생기면 조용히 그쪽을 가리켰으며, `ComponentPtr` 은 같은 타입 컴포넌트가 둘이면 첫 번째를 잡았습니다.
     */
    class GameObjectHandle
    {
    public:
        /** @brief 무효 핸들을 만듭니다. */
        constexpr GameObjectHandle() noexcept = default;

        /** @brief objectId 로 핸들을 만듭니다. 0 은 무효입니다. */
        static constexpr GameObjectHandle make( uint64 objectId ) noexcept
        {
            GameObjectHandle handle;
            handle._objectId = objectId;
            return handle;
        }

        /** @brief 가리키는 오브젝트의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 objectId() const noexcept { return _objectId; }
        /** @brief objectId 가 있으면 true 입니다. 대상이 아직 살아 있는지는 매니저로 풀어 봐야 압니다. */
        [[nodiscard]] constexpr bool     isValid() const noexcept { return _objectId != 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( GameObjectHandle lhs, GameObjectHandle rhs ) noexcept { return lhs._objectId == rhs._objectId; }
        friend constexpr bool operator!=( GameObjectHandle lhs, GameObjectHandle rhs ) noexcept { return ( lhs == rhs ) == false; }
        friend constexpr bool operator<( GameObjectHandle lhs, GameObjectHandle rhs ) noexcept { return lhs._objectId < rhs._objectId; }

    private:
        uint64 _objectId{ 0 };
    };
} // namespace sw

template <>
struct std::hash<sw::GameObjectHandle>
{
    size_t operator()( sw::GameObjectHandle handle ) const noexcept { return std::hash<uint64>{}( handle.objectId() ); }
};
