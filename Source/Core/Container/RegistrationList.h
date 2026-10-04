/**
 * @file RegistrationList.h
 * @brief 등록부의 공통 모양 — 소유하지 않는 포인터 목록에 중복 거절 · 정렬 · 이름 찾기 · 훑기 · 이름 사본을 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

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
            : _listItem{}
            , _listName{}
            , _listOrder{}
            , _order{ order }
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
                    ++insertIndex;
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
                if ( string_view{ _listName[index] } == name )
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
        bool              _bRequireName; ///< 이름 없는 등록을 거절하는지
    };
} // namespace sw
