/**
 * @file sparse_set.h
 * @brief uint32 키 → T 매핑입니다. 삽입 · 삭제 · 조회가 모두 O(1) 입니다.
 * @details packed 배치입니다. dense 키 배열과 T 배열을 나란히 두고, 삭제할 때는 둘 다 swap-remove 합니다.
 *          그래서 같은 셋에서 삽입 · 삭제가 일어나면 T 의 주소가 바뀝니다. 오래 들고 있을 때는 키나 핸들만 저장하십시오.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @brief 키와 값을 dense 배열에 촘촘히 붙여 두는 sparse set 입니다.
     */
    template <typename T>
    class sparse_set
    {
    public:
        using KeyType                        = uint32;
        using key_type                       = KeyType;
        using mapped_type                    = T;
        using value_type                     = T;
        static constexpr KeyType kInvalidKey = invalid_index::kUint32;

        sparse_set()                                   = default;
        sparse_set( const sparse_set& )                = delete;
        sparse_set& operator=( const sparse_set& )     = delete;
        sparse_set( sparse_set&& ) noexcept            = default;
        sparse_set& operator=( sparse_set&& ) noexcept = default;

        template <typename U>
        void insert( KeyType key, U&& value );

        template <typename... Args>
        void emplace( KeyType key, Args&&... args );

        void erase( KeyType key );

        size_t                 size() const { return _listDenseKey.size(); }
        const vector<KeyType>& getDenseKeys() const { return _listDenseKey; }

        void clear()
        {
            _listSparse.clear();
            _listDenseKey.clear();
            _listDenseValue.clear();
        }

        void shrink_to_fit()
        {
            _listSparse.shrink_to_fit();
            _listDenseKey.shrink_to_fit();
            _listDenseValue.shrink_to_fit();
        }

        bool contains( KeyType key ) const { return key != kInvalidKey && key < _listSparse.size() && std::as_const( _listSparse )[key] != kInvalidKey; }

        bool get( KeyType key, T& outValue ) const
        {
            if ( contains( key ) == false )
                return false;
            outValue = std::as_const( _listDenseValue )[std::as_const( _listSparse )[key]];
            return true;
        }

        T& operator[]( KeyType key )
        {
            SW_ASSERT( contains( key ) );
            return _listDenseValue[std::as_const( _listSparse )[key]];
        }

        const T& operator[]( KeyType key ) const
        {
            SW_ASSERT( contains( key ) );
            return std::as_const( _listDenseValue )[std::as_const( _listSparse )[key]];
        }

        T* find( KeyType key ) { return contains( key ) ? &_listDenseValue[std::as_const( _listSparse )[key]] : nullptr; }

        const T* find( KeyType key ) const { return contains( key ) ? &std::as_const( _listDenseValue )[std::as_const( _listSparse )[key]] : nullptr; }

        class Iterator
        {
        public:
            Iterator( sparse_set* pSet, size_t index )
                : _pSet{ pSet }
                , _index{ index } {}

            Iterator& operator++()
            {
                ++_index;
                return *this;
            }

            bool                    operator!=( const Iterator& other ) const { return _index != other._index; }
            bool                    operator==( const Iterator& other ) const { return _index == other._index; }
            std::tuple<KeyType, T&> operator*() const { return std::tuple<KeyType, T&>( _pSet->_listDenseKey[_index], _pSet->_listDenseValue[_index] ); }

        private:
            sparse_set* _pSet;
            size_t      _index;
        };

        class ConstIterator
        {
        public:
            ConstIterator( const sparse_set* pSet, size_t index )
                : _pSet{ pSet }
                , _index{ index } {}

            ConstIterator& operator++()
            {
                ++_index;
                return *this;
            }

            bool                          operator!=( const ConstIterator& other ) const { return _index != other._index; }
            bool                          operator==( const ConstIterator& other ) const { return _index == other._index; }
            std::tuple<KeyType, const T&> operator*() const { return std::tuple<KeyType, const T&>( _pSet->_listDenseKey[_index], _pSet->_listDenseValue[_index] ); }

        private:
            const sparse_set* _pSet;
            size_t            _index;
        };

        Iterator      begin() { return Iterator( this, 0 ); }
        Iterator      end() { return Iterator( this, _listDenseKey.size() ); }
        ConstIterator begin() const { return ConstIterator( this, 0 ); }
        ConstIterator end() const { return ConstIterator( this, _listDenseKey.size() ); }

    private:
        void ensureSparse( KeyType key )
        {
            if ( key >= _listSparse.size() )
                _listSparse.resize( key + 1, kInvalidKey );
        }

        vector<uint32>  _listSparse;
        vector<KeyType> _listDenseKey;
        vector<T>       _listDenseValue;
    };

    template <typename T>
    template <typename U>
    void sparse_set<T>::insert( KeyType key, U&& value )
    {
        if ( key == kInvalidKey )
            return;

        if ( contains( key ) )
        {
            _listDenseValue[_listSparse[key]] = std::forward<U>( value );
            return;
        }

        ensureSparse( key );
        _listSparse[key] = static_cast<uint32>( _listDenseKey.size() );
        _listDenseKey.push_back( key );
        _listDenseValue.push_back( std::forward<U>( value ) );
    }

    template <typename T>
    template <typename... Args>
    void sparse_set<T>::emplace( KeyType key, Args&&... args )
    {
        if ( key == kInvalidKey )
            return;

        if ( contains( key ) )
        {
            // 먼저 만들고 대입한다. 예전에는 제자리에서 소멸시킨 뒤 다시 생성했는데, 그러면 두 가지가 깨진다.
            // (1) 생성자가 예외를 던지면 이미 소멸된 칸이 남아 나중에 두 번 소멸된다.
            // (2) 인자가 그 칸 자신을 가리키면(`set.emplace( k, set[k] )`) 이미 소멸된 객체로 새 값을 만들게 된다.
            // 임시 객체를 먼저 만들고 옮겨 넣으면 둘 다 생기지 않는다.
            _listDenseValue[_listSparse[key]] = T( std::forward<Args>( args )... );
            return;
        }

        ensureSparse( key );
        _listSparse[key] = static_cast<uint32>( _listDenseKey.size() );
        _listDenseKey.push_back( key );
        _listDenseValue.emplace_back( std::forward<Args>( args )... );
    }

    template <typename T>
    void sparse_set<T>::erase( KeyType key )
    {
        if ( key == kInvalidKey || contains( key ) == false )
            return;

        const uint32 denseIndex     = _listSparse[key];
        const uint32 lastDenseIndex = static_cast<uint32>( _listDenseKey.size() - 1 );
        if ( denseIndex != lastDenseIndex )
        {
            const KeyType lastKey       = _listDenseKey[lastDenseIndex];
            _listDenseKey[denseIndex]   = lastKey;
            _listDenseValue[denseIndex] = std::move( _listDenseValue[lastDenseIndex] );
            _listSparse[lastKey]        = denseIndex;
        }
        _listDenseKey.pop_back();
        _listDenseValue.pop_back();
        _listSparse[key] = kInvalidKey;
    }

} // namespace sw
