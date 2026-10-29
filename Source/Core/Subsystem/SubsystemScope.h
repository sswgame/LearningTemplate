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
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include <type_traits>

namespace sw
{
    /**
     * @brief RTTI 없이 타입마다 고유한 64비트 해시입니다(함수 서명 글의 해시). 같은 빌드 안에서는 어느 모듈 라이브러리가 계산해도 같다.
     * @details const 를 뗀 타입으로 부른다(`locateSubsystem` 이 뗀다) — `const T` 로 찾아도 `T` 로 건 것을 찾는다.
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
    };

    /** @brief @p pScope 부터 상위로 올라가며 @p T 를 찾습니다. 어느 범위에도 없으면 nullptr 입니다. */
    template <typename T>
    T* locateSubsystem( const ISubsystemLocator* pScope )
    {
        const uint64 typeHash = getSubsystemTypeHash<std::remove_const_t<T>>();
        for ( ; pScope != nullptr; pScope = pScope->getParentScope() )
        {
            void* pFound = pScope->findInScope( typeHash );
            if ( pFound != nullptr )
                return static_cast<T*>( pFound );
        }
        return nullptr;
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
        template <class TSubsystem>
        TSubsystem& addSubsystem( unique_ptr<TSubsystem> subsystem )
        {
            TSubsystem& added       = _collection.add( std::move( subsystem ) );
            const bool  bRegistered = registerEntry( getSubsystemTypeHash<TSubsystem>(), &added, true );
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
         * @brief 이름 @p name 의 자식 범위를 만들어 매답니다(이미 있으면 그것). 자식은 이 범위가 소유하고, 자식의 부모는 이 범위입니다.
         * @details 자식이 공개한 칸은 이 범위의 조회에 보인다. 서브시스템을 더한 뒤 초기화는 부른 쪽이 한다(`getCollection().initializeAll`).
         *          `destroyAll` · 소멸은 자식을 매단 역순으로 먼저 지운 뒤 이 범위의 서브시스템을 지운다.
         */
        SubsystemScope& mountChildScope( string_view name );
        /** @brief 이름 @p name 의 자식 범위를 떼고 지웁니다(그 서브시스템을 종료 · 역순 소멸). 없으면 아무것도 하지 않습니다. */
        void unmountChildScope( string_view name );
        /** @brief 이름 @p name 의 자식 범위입니다. 없으면 nullptr 입니다. */
        SubsystemScope* findChildScope( string_view name ) const;

        /**
         * @brief 소유하지 않는 객체를 타입 @p T 로 겁니다. 객체는 범위보다 오래 살아야 하고, 먼저 사라지면 `unbind` 합니다.
         * @return 같은 타입이 이미 걸려 있으면 false 이고 걸지 않습니다(먼저 `unbind`).
         */
        template <class T>
        [[nodiscard]] bool bind( T* pObject )
        {
            return pObject != nullptr && registerEntry( getSubsystemTypeHash<T>(), static_cast<void*>( pObject ), false );
        }

        /** @brief 타입 @p T 로 빌려 건 객체를 풉니다. 소유한 서브시스템은 풀지 않습니다. */
        template <class T>
        void unbind()
        {
            unregisterBorrowed( getSubsystemTypeHash<T>() );
        }

        /** @brief 이 범위(와 매단 자식이 공개한 칸)만 봐서 @p T 를 찾습니다. 상위 범위는 보지 않습니다. 없으면 nullptr 입니다. */
        template <class T>
        T* find() const
        {
            return static_cast<T*>( findInScope( getSubsystemTypeHash<std::remove_const_t<T>>() ) );
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

        /** @brief 매단 자식 범위를 매단 역순으로 지운 뒤, 소유한 서브시스템을 모두 종료 · 소멸하고 그 조회 칸을 지웁니다. 빌려 건 객체는 남습니다. */
        void destroyAll();

        /** @brief 상위 범위를 바꿉니다(없으면 nullptr). */
        void setParentScope( const ISubsystemLocator* pParent ) { _pParent = pParent; }

        void*                    findInScope( uint64 typeHash ) const override;
        const ISubsystemLocator* getParentScope() const override { return _pParent; }

    private:
        /** @brief 조회 칸 하나입니다. */
        struct Entry
        {
            uint64 _typeHash;            ///< 걸린 타입의 해시
            void*  _pObject;             ///< 걸린 객체(그 타입으로 바꾼 주소)
            bool   _bOwned;              ///< 이 범위의 컬렉션이 소유한 서브시스템이면 true
            bool   _bPublished{ false }; ///< 매단 부모 범위의 조회에도 보이면 true(`publish`)
        };

        /** @brief 매단 자식 범위 하나입니다(이 범위가 소유한다). */
        struct MountedScope
        {
            string                     _name;  ///< 매단 이름(모듈 이름)
            unique_ptr<SubsystemScope> _scope; ///< 자식 범위
        };

        /** @brief 칸을 더합니다. 이 범위의 칸에 같은 타입이 이미 있으면 false 입니다. */
        [[nodiscard]] bool registerEntry( uint64 typeHash, void* pObject, bool bOwned );
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
        SubsystemCollection      _collection;  ///< 소유한 서브시스템
        vector<Entry>            _listEntry;   ///< 조회 칸(소유 · 빌림)
        vector<MountedScope>     _listMounted; ///< 매단 자식 범위(매단 순서)
        const ISubsystemLocator* _pParent;     ///< 상위 범위(없으면 nullptr)
    };
} // namespace sw
