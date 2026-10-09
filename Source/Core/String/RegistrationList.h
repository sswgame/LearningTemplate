/**
 * @file RegistrationList.h
 * @brief 등록부의 공통 모양 둘입니다. `RegistrationList` 는 소유하지 않는 포인터 목록(중복 거절 · 정렬 · 이름 찾기 · 훑기 · 이름 사본),
 *        `NameRegistry` 는 이름 → 값을 소유하는 표(중복 거절 또는 덮어쓰기 · 이름 찾기 · 등록 순서 훑기 · 조건 빼기)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 목록이 항목을 늘어놓는 순서입니다. */
    enum class RegistrationOrder : uint8
    {
        Insertion,       ///< 등록한 순서(빼도 남은 것의 순서는 그대로)
        ByOrderThenName, ///< (순서 값, 이름) 사전순 — 정적 초기화 순서가 번역 단위마다 다른 등록자(에디터 확장)를 위해
    };

    /** @brief `RegistrationList::add` 의 결과입니다. */
    enum class RegistrationResult : uint8
    {
        Added,          ///< 새로 올렸습니다
        AlreadyPresent, ///< 같은 객체가 이미 올라 있습니다(멱등 — 할 일이 없습니다)
        NullItem,       ///< nullptr 은 올리지 않습니다
        EmptyName,      ///< 이름을 요구하는 목록(`bRequireName`)에 이름이 비었습니다
        DuplicateName,  ///< 같은 이름의 다른 객체가 이미 올라 있습니다 — 먼저 것을 둡니다
    };

    /**
     * @class RegistrationList
     * @brief 엔진 · 에디터 등록부가 같은 모양으로 드는 목록입니다. 항목은 소유하지 않습니다(수명은 등록한 쪽).
     * @details 등록부마다 다시 짜기 쉬운 다섯 가지를 한 곳에 둡니다:
     *          - **중복 거절**: 같은 객체는 한 번만 오르고(`AlreadyPresent`), 이름을 주면 같은 이름의 다른 객체를 거절합니다(`DuplicateName`).
     *          - **정렬**: 등록 순서 또는 (순서 값, 이름). 빼기는 순서를 지킵니다.
     *          - **찾기**: 이름으로(`findByName`) · 객체로(`contains`).
     *          - **훑기**: `getItems()` 는 포인터만 든 연속 배열이라 훑는 쪽에 비용이 없습니다.
     *          - **이름 사본**: 이름은 **올릴 때** 복사합니다. 모듈이 내려간 뒤에도 그 이름으로 알리거나 찾을 수 있고, 죽은 객체의 가상 함수를 묻지 않습니다.
     *
     *          **잠금은 들지 않습니다.** 여러 스레드가 만지는 등록부(카메라 · 빛 · 모듈 언로드 리스너)는 자기 잠금 안에서 부릅니다 — 그 잠금이
     *          "가장 안쪽" 이라는 각자의 규칙을 그대로 지키게 하려는 것입니다. 항목 수가 많고 슬롯 인덱스로 O(1) 빼기를 하는 등록부(프리미티브 ·
     *          틱 · 콜라이더)는 이 모양이 아닙니다.
     */
    template <typename T>
    class RegistrationList
    {
    public:
        /** @brief 등록 순서 · 이름 선택인 빈 목록입니다. */
        RegistrationList()
            : RegistrationList( RegistrationOrder::Insertion, false )
        {
        }

        /**
         * @param order 늘어놓는 순서입니다.
         * @param bRequireName true 면 이름 없는 등록을 거절합니다(이름이 곧 id 인 목록).
         */
        RegistrationList( RegistrationOrder order, bool bRequireName )
            : RegistrationList( order, bRequireName, NameCase::CaseSensitive )
        {
        }

        /**
         * @param order 늘어놓는 순서입니다.
         * @param bRequireName true 면 이름 없는 등록을 거절합니다(이름이 곧 id 인 목록).
         * @param nameCase 이름 찾기 · 중복 판정이 대소문자를 보는지입니다(콘솔 명령처럼 사람이 치는 이름은 `IgnoreCase`).
         */
        RegistrationList( RegistrationOrder order, bool bRequireName, NameCase nameCase )
            : _listItem{}
            , _listName{}
            , _listOrder{}
            , _order{ order }
            , _nameCase{ nameCase }
            , _bRequireName{ bRequireName }
        {
        }

        /**
         * @brief 항목을 올립니다. 이름은 지금 복사합니다.
         * @param name 찾기 · 중복 판정 · 정렬에 쓰는 이름입니다. 비어 있으면 이름으로 찾을 수 없고 이름 중복도 보지 않습니다.
         * @param order `ByOrderThenName` 목록에서 앞뒤를 정하는 값입니다(작을수록 앞).
         */
        RegistrationResult add( T* pItem, string_view name = {}, int32 order = 0 )
        {
            if ( pItem == nullptr )
                return RegistrationResult::NullItem;
            if ( contains( pItem ) )
                return RegistrationResult::AlreadyPresent;
            if ( name.empty() )
            {
                if ( _bRequireName )
                    return RegistrationResult::EmptyName;
            }
            else if ( findByName( name ) != nullptr )
            {
                return RegistrationResult::DuplicateName;
            }

            size_t insertIndex = _listItem.size();
            if ( _order == RegistrationOrder::ByOrderThenName )
            {
                insertIndex = 0;
                while ( insertIndex < _listItem.size() && isBefore( _listOrder[insertIndex], _listName[insertIndex], order, name ) )
                {
                    ++insertIndex;
                }
            }
            _listItem.insert( _listItem.begin() + static_cast<ptrdiff_t>( insertIndex ), pItem );
            _listName.insert( _listName.begin() + static_cast<ptrdiff_t>( insertIndex ), string( name ) );
            _listOrder.insert( _listOrder.begin() + static_cast<ptrdiff_t>( insertIndex ), order );
            return RegistrationResult::Added;
        }

        /** @brief 그 객체가 올라 있으면 뺍니다(남은 것의 순서는 그대로). 뺐으면 true 입니다. 같은 이름의 다른 객체는 건드리지 않습니다. */
        [[nodiscard]] bool remove( const T* pItem )
        {
            const size_t index = findIndex( pItem );
            if ( index == kNotFound )
                return false;
            _listItem.erase( _listItem.begin() + static_cast<ptrdiff_t>( index ) );
            _listName.erase( _listName.begin() + static_cast<ptrdiff_t>( index ) );
            _listOrder.erase( _listOrder.begin() + static_cast<ptrdiff_t>( index ) );
            return true;
        }

        /** @brief 모두 뺍니다. */
        void clear()
        {
            _listItem.clear();
            _listName.clear();
            _listOrder.clear();
        }

        /** @brief 그 객체가 올라 있는지 봅니다. */
        bool contains( const T* pItem ) const { return findIndex( pItem ) != kNotFound; }

        /** @brief 올릴 때 복사한 이름으로 찾습니다. 없거나 이름이 비었으면 nullptr 입니다. */
        T* findByName( string_view name ) const
        {
            if ( name.empty() )
                return nullptr;
            for ( size_t index = 0; index < _listName.size(); ++index )
            {
                if ( StringUtil::equals( string_view{ _listName[index] }, name, _nameCase == NameCase::IgnoreCase ) )
                    return _listItem[index];
            }
            return nullptr;
        }

        /** @brief 올라 있는 항목 수입니다. */
        uint32 getCount() const { return static_cast<uint32>( _listItem.size() ); }
        /** @brief 순서대로 index 번째 항목입니다. */
        T* getAt( uint32 index ) const { return _listItem[index]; }
        /** @brief index 번째 항목을 올릴 때 복사한 이름입니다(없으면 빈 글). */
        const string& getNameAt( uint32 index ) const { return _listName[index]; }
        /** @brief 항목 전부(순서대로)입니다. 소유하지 않습니다. */
        const vector<T*>& getItems() const { return _listItem; }

    private:
        static constexpr size_t kNotFound = static_cast<size_t>( -1 );

        size_t findIndex( const T* pItem ) const
        {
            for ( size_t index = 0; index < _listItem.size(); ++index )
            {
                if ( _listItem[index] == pItem )
                    return index;
            }
            return kNotFound;
        }

        /** @brief (lhsOrder, lhsName) 가 (rhsOrder, rhsName) 보다 앞이면 true 입니다. */
        static bool isBefore( int32 lhsOrder, const string& lhsName, int32 rhsOrder, string_view rhsName )
        {
            if ( lhsOrder != rhsOrder )
                return lhsOrder < rhsOrder;
            return string_view{ lhsName } < rhsName;
        }

    private:
        vector<T*>        _listItem;     ///< 항목(순서대로). 훑기가 이것만 읽는다
        vector<string>    _listName;     ///< 항목마다 올릴 때 복사한 이름(같은 자리)
        vector<int32>     _listOrder;    ///< 항목마다 정렬 값(같은 자리)
        RegistrationOrder _order;        ///< 늘어놓는 순서
        NameCase          _nameCase;     ///< 이름 비교가 대소문자를 보는지
        bool              _bRequireName; ///< 이름 없는 등록을 거절하는지
    };
} // namespace sw

namespace sw
{
    /**
     * @class NameRegistry
     * @brief 이름(`hashed_string`, 대소문자 무시) → 값을 **소유하는** 등록부입니다. 값은 등록 순서로 연속 배열에 둡니다.
     * @details 값을 들고 있어야 하는 표(변환기 · 활동 종류 · 연산 · 패널 그리기 함수)가 같은 모양으로 씁니다. 빈 이름은 받지 않습니다.
     *          `find` 가 돌려준 포인터는 다음 `add` · `addOrReplace`(새 이름) · `removeIf` · `clear` 까지만 유효합니다.
     *          잠금은 들지 않습니다 — 여러 스레드가 만지면 쓰는 쪽이 자기 잠금 안에서 부릅니다.
     */
    template <typename T>
    class NameRegistry
    {
    public:
        NameRegistry()
            : _listName{}
            , _listValue{}
        {
        }

        /** @brief 값을 올립니다. 이름이 비었거나 같은 이름이 이미 있으면 올리지 않고 false 입니다(먼저 것을 둡니다). */
        bool add( const hashed_string& name, T value )
        {
            if ( name.empty() || findIndex( name ) != kNotFound )
                return false;
            _listName.push_back( name );
            _listValue.push_back( std::move( value ) );
            return true;
        }

        /** @brief 값을 올립니다. 같은 이름이 있으면 그 자리(순서)에서 값을 바꿉니다. 빈 이름은 무시합니다. */
        void addOrReplace( const hashed_string& name, T value )
        {
            if ( name.empty() )
                return;
            const size_t index = findIndex( name );
            if ( index != kNotFound )
            {
                _listName[index]  = name;
                _listValue[index] = std::move( value );
                return;
            }
            _listName.push_back( name );
            _listValue.push_back( std::move( value ) );
        }

        /** @brief 이름의 값입니다. 없으면 nullptr 입니다. */
        T* find( const hashed_string& name )
        {
            const size_t index = findIndex( name );
            return index == kNotFound ? nullptr : &_listValue[index];
        }

        /** @brief 이름의 값입니다. 없으면 nullptr 입니다. */
        const T* find( const hashed_string& name ) const
        {
            const size_t index = findIndex( name );
            return index == kNotFound ? nullptr : &_listValue[index];
        }

        /** @brief @p predicate( const T& ) 가 true 인 값을 모두 뺍니다(남은 것의 순서는 그대로). 뺀 수입니다. */
        template <typename Predicate>
        uint32 removeIf( Predicate predicate )
        {
            uint32 removedCount = 0;
            for ( size_t index = _listValue.size(); index > 0; --index )
            {
                if ( predicate( static_cast<const T&>( _listValue[index - 1] ) ) == false )
                    continue;
                _listName.erase( _listName.begin() + static_cast<ptrdiff_t>( index - 1 ) );
                _listValue.erase( _listValue.begin() + static_cast<ptrdiff_t>( index - 1 ) );
                ++removedCount;
            }
            return removedCount;
        }

        /** @brief 모두 뺍니다. */
        void clear()
        {
            _listName.clear();
            _listValue.clear();
        }

        /** @brief 올라 있는 값 수입니다. */
        uint32 getCount() const { return static_cast<uint32>( _listValue.size() ); }
        /** @brief index 번째 값을 올린 이름입니다. */
        const hashed_string& getNameAt( uint32 index ) const { return _listName[index]; }
        /** @brief 값 전부(등록 순서)입니다. */
        const vector<T>& getItems() const { return _listValue; }

    private:
        static constexpr size_t kNotFound = static_cast<size_t>( -1 );

        size_t findIndex( const hashed_string& name ) const
        {
            if ( name.empty() )
                return kNotFound;
            for ( size_t index = 0; index < _listName.size(); ++index )
            {
                if ( _listName[index] == name )
                    return index;
            }
            return kNotFound;
        }

    private:
        vector<hashed_string> _listName;  ///< 값마다 올린 이름(같은 자리)
        vector<T>             _listValue; ///< 값(등록 순서)
    };
} // namespace sw
