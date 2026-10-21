/**
 * @file ComponentHandle.h
 * @brief 컴포넌트 핸들입니다. 보관할 때는 핸들을 쓰고, T* 는 핸들을 푼 그 순간에만 씁니다.
 *
 * @note `SlotHandle` 과 같은 곳(`Core/Container`)에 둡니다. objectID 와 componentID 두 정수뿐인 값 타입이라 Core 밖을
 *       전혀 모릅니다. 그래서 직렬화기가 Object 를 include 하지 않고 이 핸들의 텍스트 핸들러를 등록합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 타입을 지운 컴포넌트 핸들입니다.
     * @details GameObject 가 소유한 컴포넌트를 objectID + componentID 로 식별합니다.
     */
    class ComponentHandle
    {
    public:
        /** @brief 무효 핸들을 만듭니다. */
        constexpr ComponentHandle() noexcept = default;

        /** @brief GameObject 가 소유한 컴포넌트의 핸들을 만듭니다. */
        static constexpr ComponentHandle makeOwned( uint64 objectID, uint64 componentID ) noexcept
        {
            ComponentHandle handle;
            handle._objectID    = objectID;
            handle._componentID = componentID;
            return handle;
        }

        /** @brief 소유 GameObject 의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 objectID() const noexcept { return _objectID; }
        /** @brief 컴포넌트 인스턴스의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 componentID() const noexcept { return _componentID; }
        /** @brief objectID 와 componentID 가 모두 있으면 true 입니다. 대상이 살아 있는지는 매니저로 조회해 봐야 압니다. */
        [[nodiscard]] constexpr bool     isValid() const noexcept { return _objectID != 0 && _componentID != 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( ComponentHandle lhs, ComponentHandle rhs ) noexcept
        {
            return lhs._objectID == rhs._objectID && lhs._componentID == rhs._componentID;
        }
        friend constexpr bool operator!=( ComponentHandle lhs, ComponentHandle rhs ) noexcept { return ( lhs == rhs ) == false; }

    private:
        uint64 _objectID{ 0 };
        uint64 _componentID{ 0 };
    };
} // namespace sw

namespace sw
{
    class GameObjectManager;

    /**
     * @brief 타입 없는 핸들에서 타입 핸들을 만드는 생성자의 패스키입니다. 생성자를 `GameObjectManager` 만 부를 수 있습니다.
     * @details 타입 없는 핸들을 타입 핸들로 바꾸는 길은 대상의 타입을 확인하는 `GameObjectManager::makeTypedHandle` 하나다.
     *          누구나 부를 수 있는 변환을 두면 확인 없이 다른 타입의 컴포넌트를 가리키는 타입 핸들이 생긴다.
     */
    class ComponentHandleCastKey
    {
        friend class GameObjectManager;
        // `= default` 로 두면 C++17 에서 집합체가 되어 `ComponentHandleCastKey{}` 로 누구나 만들 수 있다. 사용자 정의 생성자로 둔다.
        constexpr ComponentHandleCastKey() noexcept {} // NOLINT(modernize-use-equals-default)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 대상 타입 @p T 를 컴파일 시간 정보로 실은 컴포넌트 핸들입니다. 크기와 복사 규칙은 `ComponentHandle` 과 같습니다(16 B, trivially copyable).
     * @details 핸들은 id 만 담으므로, 타입 없는 핸들을 찾은 뒤 `static_cast` 하면 다른 타입의 슬롯을 그 타입으로 읽을 수 있다. 타입 핸들은 그 실수를 형식으로 막는다.
     *          - `T*` 에서 만들면 확인이 없다(타입이 컴파일 시간에 정해진다). 파생 타입 핸들은 기반 타입 핸들로 암시적으로 바뀐다(업캐스트).
     *          - 타입 없는 핸들이나 기반 타입 핸들에서 만드는 경로는 `GameObjectManager::makeTypedHandle` 뿐이다(대상의 타입을 확인한다).
     *          - 찾기는 `GameObjectManager::resolveComponent( TComponentHandle<T> )` 이고 `T*` 를 돌려준다.
     *          - 타입 없는 `ComponentHandle` 과의 `==` 는 허용한다. 비교는 id 만 보고 주소를 만들지 않는다. 서로 관계없는 두 타입 핸들의 비교는 컴파일 오류다.
     *          Core 는 `Component` 를 모른다. `T` 의 멤버는 템플릿이 인스턴스화될 때만 본다.
     * @note `PROPERTY()` 에는 이 타입 핸들 한 값과 그 시퀀스만 둘 수 있다. 파서가 값을 내장 `ComponentHandle` 로 기록하고 대상 `T` 를 따로 싣는다. 파일에는
     *       `{대상 오브젝트의 저장 id, 안정 키}` 로 적는다(`docs/plans/TypedComponents.md` 3.7).
     */
    template <typename T>
    class TComponentHandle
    {
    public:
        /** @brief 무효 핸들("대상 없음")을 만듭니다. */
        constexpr TComponentHandle() noexcept = default;

        /** @brief @p pComponent 를 가리키는 핸들을 만듭니다. nullptr 이거나 소유 오브젝트가 없으면 무효 핸들입니다. */
        explicit TComponentHandle( const T* pComponent ) noexcept
            : _handle{ pComponent != nullptr ? pComponent->getHandle() : ComponentHandle{} }
        {
        }

        /** @brief 파생 타입 @p U 의 핸들에서 업캐스트합니다. 확인이 필요 없으므로 암시적입니다. */
        template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U> && std::is_same_v<T, U> == false>>
        constexpr TComponentHandle( const TComponentHandle<U>& other ) noexcept // NOLINT(google-explicit-constructor): 업캐스트는 암시적이다
            : _handle{ other.untyped() }
        {
        }

        /** @brief 타입을 확인한 쪽(`GameObjectManager::makeTypedHandle`)만 부릅니다. */
        constexpr TComponentHandle( ComponentHandleCastKey, ComponentHandle handle ) noexcept
            : _handle{ handle }
        {
        }

        /** @brief 타입을 지운 핸들입니다. 레지스트리, 지연 파괴 목록처럼 타입을 모르는 곳에 넘길 때 씁니다. */
        [[nodiscard]] constexpr ComponentHandle untyped() const noexcept { return _handle; }
        /** @brief 소유 GameObject 의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 objectID() const noexcept { return _handle.objectID(); }
        /** @brief 컴포넌트 인스턴스의 ID 를 반환합니다. */
        [[nodiscard]] constexpr uint64 componentID() const noexcept { return _handle.componentID(); }
        /** @brief objectID 와 componentID 가 모두 있으면 true 입니다. 대상이 살아 있는지는 매니저로 조회해 봐야 압니다. */
        [[nodiscard]] constexpr bool     isValid() const noexcept { return _handle.isValid(); }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return isValid(); }

        friend constexpr bool operator==( const TComponentHandle& lhs, const TComponentHandle& rhs ) noexcept { return lhs._handle == rhs._handle; }
        friend constexpr bool operator!=( const TComponentHandle& lhs, const TComponentHandle& rhs ) noexcept { return lhs._handle != rhs._handle; }
        friend constexpr bool operator==( const TComponentHandle& lhs, ComponentHandle rhs ) noexcept { return lhs._handle == rhs; }
        friend constexpr bool operator!=( const TComponentHandle& lhs, ComponentHandle rhs ) noexcept { return lhs._handle != rhs; }
        friend constexpr bool operator==( ComponentHandle lhs, const TComponentHandle& rhs ) noexcept { return lhs == rhs._handle; }
        friend constexpr bool operator!=( ComponentHandle lhs, const TComponentHandle& rhs ) noexcept { return lhs != rhs._handle; }

    private:
        ComponentHandle _handle{};
    };
} // namespace sw
