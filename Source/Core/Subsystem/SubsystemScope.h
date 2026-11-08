/**
 * @file Core/Subsystem/SubsystemScope.h
 * @brief 범위(scope)를 가진 서브시스템 묶음과 그 읽기 전용 조회 창구입니다 — 타입으로 찾고, 이 범위에 없으면 상위 범위를 봅니다.
 * @details 언리얼의 서브시스템 범위(엔진 · 에디터 · 게임 인스턴스, `GetSubsystem<T>()`)와 같은 자리입니다. 범위 하나는
 *          - 소유하는 서브시스템(`addSubsystem`, `SubsystemCollection` 이 의존 순서로 세우고 내린다)과
 *          - 소유하지 않는 객체(`bind`, 게임 카탈로그 · 테스트의 스택 객체 — 건 쪽이 범위보다 오래 살아야 한다)를
 *          타입 해시로 찾게 합니다. 조회(`locateSubsystem`)는 그 범위에서 찾고 없으면 상위 범위로 올라갑니다.
 *          범위는 그것을 든 객체(게임 인스턴스 · 에디터 컨텍스트)와 수명이 같아, 모듈 라이브러리가 내려갈 때 그 객체와 함께 사라진다 —
 *          "건 칸을 풀지 않고 내려간 이미지" 를 훑어 회수할 일이 없다.
 *          **매단 자식 범위**(`mountChildScope`): 범위가 이름 붙은 자식 범위를 소유하고, 자식이 **공개한**(`publish`) 칸은 이 범위의 조회에도 보인다.
 *          모듈 라이브러리 하나가 자기 서브시스템을 프로세스 · 엔진 범위에 더했다가 내려갈 때 통째로 걷는 자리다(`ModuleSubsystemRegistrar`) —
 *          언리얼이 모듈을 올릴 때 그 모듈의 `UEngineSubsystem` 을 컬렉션에 더하고 내릴 때 빼는 것과 같다. 자식의 부모는 이 범위라 자식 서브시스템은 위로 찾는다.
 * @note 같은 범위에 같은 타입을 두 번 걸 수 없다(`bind` · `expose` 가 false). 조회는 선형 검색이라 핫 패스에서는 한 번 얻은 포인터를 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include <type_traits>

namespace sw
{
    /**
     * @brief RTTI 없이 타입마다 고유한 64비트 해시입니다(함수 서명 글의 해시). 같은 빌드 안에서는 어느 모듈 라이브러리가 계산해도 같다.
     * @details `T` 와 `const T` 는 해시가 다르다 — 칸의 const 가 곧 그 해시다(`SubsystemScope::bind`). 찾는 규칙은 `findInLocator` 한 곳에 있다.
     */
    template <typename T>
    constexpr uint64 getSubsystemTypeHash() noexcept
    {
        return HashUtil::computeStringHash64( SW_FUNCTION_SIGNATURE, sizeof( SW_FUNCTION_SIGNATURE ) - 1, false );
    }

    /**
     * @class ISubsystemLocator
     * @brief 범위 하나의 읽기 전용 조회 창구입니다. 타입 해시로 이 범위만 찾고, 상위 범위를 알려 줍니다.
     */
    class ISubsystemLocator
    {
    public:
        virtual ~ISubsystemLocator() = default;

        /** @brief 이 범위에 @p typeHash 로 걸린 객체입니다. 없으면 nullptr 이고, 상위 범위는 보지 않습니다. */
        virtual void* findInScope( uint64 typeHash ) const = 0;
        /** @brief 상위 범위입니다. 없으면 nullptr 입니다. */
        virtual const ISubsystemLocator* getParentScope() const = 0;
        /**
         * @brief 이 범위의 칸 중 호스트 모듈(에디터)에 보인다고 선언한 것(`SubsystemModuleVisibility::HostModules`)에서만 @p typeHash 를 찾습니다. 상위는 보지 않습니다.
         * @details 호스트 모듈 창구(`ModuleRegistrationHost::getHostModuleLocator`)가 엔진 범위부터 상위로 올라가며 묻는다. 칸을 갖지 않는 창구는 없다(nullptr).
         */
        virtual void* findHostModuleVisibleInScope( uint64 typeHash ) const
        {
            (void)typeHash;
            return nullptr;
        }
    };

    /**
     * @brief 범위 하나에서 @p T 를 찾습니다(상위는 보지 않는다). `const T` 로 찾으면 const 칸과 일반 칸을 모두 보고, `T` 로 찾으면 일반 칸만 봅니다.
     * @details const 로 건 객체(공유 캐시의 카탈로그)는 `T*` 로 내주지 못한다 — 형식이 쓰기를 막으므로 찾는 쪽이 const 를 적어야 찾는다.
     */
    template <typename T>
    T* findInLocator( const ISubsystemLocator& scope )
    {
        void* pFound = scope.findInScope( getSubsystemTypeHash<T>() );
        if constexpr ( std::is_const_v<T> )
        {
            if ( pFound == nullptr )
                pFound = scope.findInScope( getSubsystemTypeHash<std::remove_const_t<T>>() );
        }
        return static_cast<T*>( pFound );
    }

    /** @brief @p pScope 부터 상위로 올라가며 @p T 를 찾습니다. 어느 범위에도 없으면 nullptr 입니다. */
    template <typename T>
    T* locateSubsystem( const ISubsystemLocator* pScope )
    {
        for ( ; pScope != nullptr; pScope = pScope->getParentScope() )
        {
            T* const pFound = findInLocator<T>( *pScope );
            if ( pFound != nullptr )
                return pFound;
        }
        return nullptr;
    }

    /** @brief 범위 @p scope 부터 찾은, 반드시 있어야 하는 @p T 입니다. 없으면 그 실행의 잘못이라 모든 구성에서 멈춥니다. */
    template <typename T>
    T& requireSubsystem( const ISubsystemLocator& scope )
    {
        T* const pFound = locateSubsystem<T>( &scope );
        if ( pFound == nullptr )
            ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
        return *pFound;
    }
} // namespace sw

namespace sw
{
    /**
     * @class SubsystemScope
     * @brief 서브시스템을 소유하고 객체를 빌려 걸어, 타입으로 찾게 하는 범위입니다. 소멸하면 소유한 서브시스템을 역순으로 내리고 지웁니다.
     */
    class SW_API SubsystemScope final : public ISubsystemLocator
    {
    public:
        explicit SubsystemScope( const ISubsystemLocator* pParent = nullptr );
        /** @brief 소유한 서브시스템을 `destroyAll` 과 같이 종료 · 역순 소멸합니다. 빌려 건 객체는 건드리지 않습니다. */
        ~SubsystemScope() override;

        SubsystemScope( const SubsystemScope& )            = delete;
        SubsystemScope& operator=( const SubsystemScope& ) = delete;

        /**
         * @brief 서브시스템을 넘겨받아 컬렉션에 더하고 타입 @p TSubsystem 으로 찾게 합니다. 넘긴 객체를 돌려줍니다(소유는 범위).
         * @details 같은 타입이 이미 걸려 있으면 컬렉션에는 더하지만 조회에는 걸지 않는다 — 부른 쪽이 잘못이라 `SW_ASSERT` 로 멈춘다.
         */
        /** @brief @p args 로 서브시스템을 만들어 더합니다. 만드는 동안 그 형식의 용도 태그(`kSubsystemMemoryTag`)를 겁니다 — 부르는 쪽이 태그를 적지 않는다. */
        template <class TSubsystem, class... TArgs>
        TSubsystem& emplaceSubsystem( TArgs&&... args )
        {
            const ScopedMemoryTag memoryScope{ TSubsystem::kSubsystemMemoryTag };
            return addSubsystem( sw::make_unique<TSubsystem>( std::forward<TArgs>( args )... ) );
        }

        template <class TSubsystem>
        TSubsystem& addSubsystem( unique_ptr<TSubsystem> subsystem )
        {
            TSubsystem& added       = _collection.add( std::move( subsystem ) );
            const bool  bRegistered = registerEntry( getSubsystemTypeHash<TSubsystem>(), &added, true, TSubsystem::kModuleVisibility );
            SW_ASSERT( bRegistered );
            (void)bRegistered;
            return added;
        }

        /**
         * @brief 이미 걸린 객체 @p object 를 인터페이스 @p TInterface 로도 찾게 합니다(예: `EditorPanelService` 를 `IEditorPanelService` 로).
         * @return 그 인터페이스가 이미 걸려 있으면 false 입니다.
         */
        template <class TInterface, class TObject>
        [[nodiscard]] bool expose( TObject& object )
        {
            static_assert( std::is_base_of_v<TInterface, TObject>, "SubsystemScope::expose needs a base interface" );
            // 소유한 서브시스템을 드러낸 칸은 그 서브시스템과 함께 지워져야 한다(destroyAll).
            return registerEntry( getSubsystemTypeHash<TInterface>(), static_cast<TInterface*>( &object ), isOwnedEntry( getSubsystemTypeHash<TObject>() ) );
        }

        /**
         * @brief 이미 걸린 객체 @p owner 가 가진 멤버 @p member 를 인터페이스 @p TInterface 로 찾게 합니다 — 인터페이스를 상속하지 않고 멤버 어댑터로 가진 서브시스템의 자리다.
         * @details 칸의 소유 표시는 @p owner 를 따른다(소유한 서브시스템의 멤버면 그 서브시스템과 함께 지워진다 — `destroyAll`). @p member 는 @p owner 와 수명이 같아야 한다.
         * @return 그 인터페이스가 이미 걸려 있으면 false 입니다.
         */
        template <class TInterface, class TOwner>
        [[nodiscard]] bool exposeMember( TOwner& owner, TInterface& member )
        {
            (void)owner;
            return registerEntry( getSubsystemTypeHash<TInterface>(), &member, isOwnedEntry( getSubsystemTypeHash<TOwner>() ) );
        }

        /**
         * @brief 이미 걸린 객체 @p object 를 타입 @p TInterface 로 **공개**합니다 — 이 범위를 매단 부모 범위의 조회(`findInScope`)에도 보입니다.
         * @details 낮은 층이 높은 층의 구현을 받는 자리(공개 슬롯)다. 함수 칸 · 정적 포인터를 두고 높은 층이 채우게 하는 대신, 높은 층의 서브시스템이 자기 모듈 범위에서
         *          인터페이스를 공개하고 낮은 층은 그 인터페이스 타입으로 찾는다(언리얼 `IModularFeatures` 의 구현 등록과 같은 자리). @p TInterface 가 이미 같은 객체로
         *          걸려 있으면(소유한 서브시스템의 제 타입) 공개 표시만 더한다.
         * @return 같은 타입이 다른 객체로 이미 걸려 있으면 false 입니다.
         */
        template <class TInterface, class TObject>
        [[nodiscard]] bool publish( TObject& object )
        {
            static_assert( std::is_base_of_v<TInterface, TObject>, "SubsystemScope::publish needs a base interface" );
            return publishEntry( getSubsystemTypeHash<TInterface>(), static_cast<TInterface*>( &object ), isOwnedEntry( getSubsystemTypeHash<TObject>() ) );
        }

        /**
         * @brief 이미 걸린 객체 @p owner 가 멤버로 든 @p part 를 타입 @p TInterface 로 **공개**합니다 — 인터페이스를 상속하지 않고 멤버 객체로 든 서브시스템이 쓴다.
         * @details 칸의 소유 여부는 @p owner 의 칸을 따른다 — 멤버는 주인과 수명이 같아, 소유한 서브시스템의 멤버 칸은 `destroyAll` 에서 함께 지워진다.
         * @return 같은 타입이 다른 객체로 이미 걸려 있으면 false 입니다.
         */
        template <class TInterface, class TOwner, class TPart>
        [[nodiscard]] bool publishPart( TOwner& owner, TPart& part )
        {
            static_assert( std::is_base_of_v<TInterface, TPart>, "SubsystemScope::publishPart needs a base interface of the part" );
            (void)owner; // 소유 여부를 정하는 타입만 쓴다
            return publishEntry( getSubsystemTypeHash<TInterface>(), static_cast<TInterface*>( &part ), isOwnedEntry( getSubsystemTypeHash<TOwner>() ) );
        }

        /**
         * @brief 이름 @p name 의 자식 범위를 만들어 매답니다(이미 있으면 그것). 자식은 이 범위가 소유하고, 자식의 부모는 이 범위입니다.
         * @details 자식이 공개한 칸은 이 범위의 조회에 보인다. 서브시스템을 더한 뒤 초기화는 부른 쪽이 한다(`getCollection().initializeAll`).
         *          `destroyAll` · 소멸은 자식을 매단 역순으로 먼저 지운 뒤 이 범위의 서브시스템을 지운다.
         */
        SubsystemScope& mountChildScope( string_view name );
        /** @brief 이름 @p name 의 자식 범위를 떼고 지웁니다(그 서브시스템을 종료 · 역순 소멸). 없으면 아무것도 하지 않습니다. */
        void unmountChildScope( string_view name );
        /** @brief 이름 @p name 의 자식 범위입니다. 없으면 nullptr 입니다. */
        SubsystemScope* findChildScope( string_view name ) const;
        /** @brief 매단 자식 범위가 공개한 칸에서만 찾습니다(이 범위 자신의 칸 · 상위 범위는 보지 않는다). 모듈에 넘기는 "모듈이 공개한 것" 창구가 쓴다. 없으면 nullptr 입니다. */
        void* findInMounted( uint64 typeHash ) const;

        /**
         * @brief 소유하지 않는 객체를 타입 @p T 로 겁니다. 객체는 범위보다 오래 살아야 하고, 먼저 사라지면 `unbind` 합니다.
         * @details @p T 가 const 면(공유 캐시의 카탈로그) `const T` 로 찾는 쪽만 찾는다(`findInLocator`). 칸의 `void*` 는 형식을 지운 저장일 뿐 const 는 해시에 남는다.
         * @return 같은 타입이 const 든 아니든 이미 걸려 있으면 false 이고 걸지 않습니다(먼저 `unbind`).
         */
        template <class T>
        [[nodiscard]] bool bind( T* pObject )
        {
            using TPlain = std::remove_const_t<T>;
            if ( pObject == nullptr || findOwnEntry( getSubsystemTypeHash<TPlain>() ) != nullptr || findOwnEntry( getSubsystemTypeHash<const TPlain>() ) != nullptr )
                return false;
            return registerEntry( getSubsystemTypeHash<T>(), const_cast<TPlain*>( pObject ), false );
        }

        /** @brief 타입 @p T(건 때와 같은 const)로 빌려 건 객체를 풉니다. 소유한 서브시스템은 풀지 않습니다. */
        template <class T>
        void unbind()
        {
            unregisterBorrowed( getSubsystemTypeHash<T>() );
        }

        /** @brief 이 범위(와 매단 자식이 공개한 칸)만 봐서 @p T 를 찾습니다. 상위 범위는 보지 않습니다. 없으면 nullptr 입니다. */
        template <class T>
        T* find() const
        {
            return findInLocator<T>( *this );
        }

        /** @brief 이 범위에 있어야 하는 @p T 입니다. 없으면 부른 쪽의 잘못이라 모든 구성에서 오류를 남기고 멈춥니다(`internal::reportMissingRequiredService`, 소유한 서브시스템을 초기화 뒤에 묻는 자리). */
        template <class T>
        T& get() const
        {
            T* pFound = find<T>();
            if ( pFound == nullptr )
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            return *pFound;
        }

        /** @brief 소유한 서브시스템 목록입니다(정렬 · 초기화 · 종료는 이것으로 한다). */
        SubsystemCollection&       getCollection() { return _collection; }
        const SubsystemCollection& getCollection() const { return _collection; }

        /**
         * @brief 매단 자식 범위를 매단 역순으로 지운 뒤, 소유한 서브시스템을 모두 종료합니다(소멸은 `destroyAll`).
         * @details 자식 서브시스템은 이 범위의 서브시스템에 기댄다(모듈의 데이터 캐시가 에셋 매니저에 올라 있다) — 소멸뿐 아니라 종료도 자식이 먼저다.
         *          컬렉션만 종료하면 자식이 아직 등록을 쥔 채 부모가 내려간다(배포의 `Asset cache '…' is still registered`).
         */
        void shutdownAll();
        /** @brief 매단 자식 범위를 매단 역순으로 지운 뒤, 소유한 서브시스템을 모두 종료 · 소멸하고 그 조회 칸을 지웁니다. 빌려 건 객체는 남습니다. */
        void destroyAll();

        /** @brief 상위 범위를 바꿉니다(없으면 nullptr). */
        void setParentScope( const ISubsystemLocator* pParent ) { _pParent = pParent; }

        void*                    findInScope( uint64 typeHash ) const override;
        const ISubsystemLocator* getParentScope() const override { return _pParent; }
        void*                    findHostModuleVisibleInScope( uint64 typeHash ) const override;

    private:
        /** @brief 조회 칸 하나입니다. */
        struct Entry
        {
            uint64                    _typeHash;                                                 ///< 걸린 타입의 해시
            void*                     _pObject;                                                  ///< 걸린 객체(그 타입으로 바꾼 주소)
            bool                      _bOwned;                                                   ///< 이 범위의 컬렉션이 소유한 서브시스템이면 true NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            bool                      _bPublished{ false };                                      ///< 매단 부모 범위의 조회에도 보이면 true(`publish`) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            SubsystemModuleVisibility _moduleVisibility{ SubsystemModuleVisibility::ScopeOnly }; ///< 모듈 서비스 범위에도 보이는지(소유한 서브시스템의 `kModuleVisibility`)
        };

        /** @brief 매단 자식 범위 하나입니다(이 범위가 소유한다). */
        struct MountedScope
        {
            string                     _name;  ///< 매단 이름(모듈 이름)
            unique_ptr<SubsystemScope> _scope; ///< 자식 범위
        };

        /** @brief 매단 자식 범위를 매단 역순으로 지웁니다(`shutdownAll` · `destroyAll` 의 첫 단계). */
        void destroyMountedScopes();
        /** @brief 칸을 더합니다. 이 범위의 칸에 같은 타입이 이미 있으면 false 입니다. */
        [[nodiscard]] bool registerEntry( uint64 typeHash, void* pObject, bool bOwned, SubsystemModuleVisibility moduleVisibility = SubsystemModuleVisibility::ScopeOnly );
        /** @brief 칸을 공개합니다. 같은 객체로 이미 걸렸으면 표시만 더하고, 다른 객체로 걸렸으면 false 입니다. */
        [[nodiscard]] bool publishEntry( uint64 typeHash, void* pObject, bool bOwned );
        /** @brief 이 범위 자신의 칸에서만 찾습니다(자식 · 상위는 보지 않는다). */
        const Entry* findOwnEntry( uint64 typeHash ) const;
        /** @brief 공개한 칸을 이 범위와 그 자식에서 찾습니다(매단 부모가 부른다). */
        void* findPublished( uint64 typeHash ) const;
        /** @brief 빌려 건 칸 하나를 지웁니다. */
        void unregisterBorrowed( uint64 typeHash );
        /** @brief @p typeHash 칸이 소유한 서브시스템이면 true 입니다. */
        bool isOwnedEntry( uint64 typeHash ) const;

    private:
        SubsystemCollection _collection; ///< 소유한 서브시스템
        /// @brief 조회 칸(소유 · 빌림)입니다. 범위 대부분(호스트 · 모듈 · 키트)은 칸이 열 몇 개라 인라인에 든다(언리얼 `TInlineAllocator` 자리) — 기동 뒤에 칸을 빌려 걸고
        ///        풀어도(시나리오 재생기) 힙 저장소가 새로 자라지 않는다. 힙이면 그 저장소가 허물어도 남는 빌린 칸과 함께 종료 누수 보고에 찍혔다.
        small_vector<Entry, 16>  _listEntry;
        vector<MountedScope>     _listMounted; ///< 매단 자식 범위(매단 순서)
        const ISubsystemLocator* _pParent;     ///< 상위 범위(없으면 nullptr)
    };
} // namespace sw
