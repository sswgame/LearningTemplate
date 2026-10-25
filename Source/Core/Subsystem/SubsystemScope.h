/**
 * @file Core/Subsystem/SubsystemScope.h
 * @brief 범위(scope)를 가진 서브시스템 묶음과 그 읽기 전용 조회 창구입니다 — 타입으로 찾고, 이 범위에 없으면 상위 범위를 봅니다.
 * @details 언리얼의 서브시스템 범위(엔진 · 에디터 · 게임 인스턴스, `GetSubsystem<T>()`)와 같은 자리입니다. 범위 하나는
 *          - 소유하는 서브시스템(`addSubsystem`, `SubsystemCollection` 이 의존 순서로 세우고 내린다)과
 *          - 소유하지 않는 객체(`bind`, 게임 카탈로그 · 테스트의 스택 객체 — 건 쪽이 범위보다 오래 살아야 한다)를
 *          타입 해시로 찾게 합니다. 조회(`locateSubsystem`)는 그 범위에서 찾고 없으면 상위 범위로 올라갑니다.
 *          범위는 그것을 든 객체(게임 인스턴스 · 에디터 컨텍스트)와 수명이 같아, 모듈 이미지가 내려갈 때 그 객체와 함께 사라진다 —
 *          "건 칸을 풀지 않고 내려간 이미지" 를 훑어 회수할 일이 없다.
 * @note 같은 범위에 같은 타입을 두 번 걸 수 없다(`bind` · `expose` 가 false). 조회는 선형 검색이라 핫 패스에서는 한 번 얻은 포인터를 든다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemCollection.h"

#include <type_traits>

namespace sw
{
    /**
     * @brief RTTI 없이 타입마다 고유한 64비트 해시입니다(함수 서명 글의 해시). 같은 빌드 안에서는 어느 모듈 이미지가 계산해도 같다.
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
         * @brief 이미 걸린 객체 @p object 를 인터페이스 @p TInterface 로도 찾게 합니다(예: `EditorPanelHost` 를 `IEditorPanelHost` 로).
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

        /** @brief 이 범위만 봐서 @p T 를 찾습니다. 없으면 nullptr 입니다. */
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

        /** @brief 소유한 서브시스템을 모두 종료 · 소멸하고 그 조회 칸을 지웁니다. 빌려 건 객체는 남습니다. */
        void destroyAll();

        /** @brief 상위 범위를 바꿉니다(없으면 nullptr). */
        void setParentScope( const ISubsystemLocator* pParent ) { _pParent = pParent; }

        void*                    findInScope( uint64 typeHash ) const override;
        const ISubsystemLocator* getParentScope() const override { return _pParent; }

    private:
        /** @brief 조회 칸 하나입니다. */
        struct Entry
        {
            uint64 _typeHash; ///< 걸린 타입의 해시
            void*  _pObject;  ///< 걸린 객체(그 타입으로 바꾼 주소)
            bool   _bOwned;   ///< 이 범위의 컬렉션이 소유한 서브시스템이면 true
        };

        /** @brief 칸을 더합니다. 같은 타입이 이미 있으면 false 입니다. */
        bool registerEntry( uint64 typeHash, void* pObject, bool bOwned );
        /** @brief 빌려 건 칸 하나를 지웁니다. */
        void unregisterBorrowed( uint64 typeHash );
        /** @brief @p typeHash 칸이 소유한 서브시스템이면 true 입니다. */
        bool isOwnedEntry( uint64 typeHash ) const;

    private:
        SubsystemCollection      _collection; ///< 소유한 서브시스템
        vector<Entry>            _listEntry;  ///< 조회 칸(소유 · 빌림)
        const ISubsystemLocator* _pParent;    ///< 상위 범위(없으면 nullptr)
    };
} // namespace sw
