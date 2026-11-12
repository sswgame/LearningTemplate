/**
 * @file WeakInternTable.h
 * @brief 키 → 약한 참조 값 표 하나입니다. "같은 키면 같은 객체를 나눠 주고, 마지막 사용자가 놓으면 사라지는" 캐시가 모두 이 구현을 씁니다.
 * @details 경로로 읽는 에셋 표(`SharedAssetTable` — 스켈레톤 · 클립 · 리그 · 스프라이트 클립 · 캐릭터 데이터)와 코드로 짓는 값 표(`WeakInternCache` —
 *          내장 도형 · 9-슬라이스 메시 · 스프라이트 텍스처 인스턴스)가 같은 표를 씁니다. 값을 짓는 일(파일 IO · 메시 생성)은 잠금 밖에서 하고,
 *          둘이 같은 키를 동시에 지으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받습니다. 마지막 사용자가 놓으면 그 값의 삭제자가 자기 칸을 지웁니다 —
 *          표에 만료 칸이 쌓이지 않으므로 넣기 · 찾기는 해시 조회 하나입니다(언리얼 · 유니티의 에셋 표도 해시 조회이고, 표 전체를 훑는 수거를 넣기 길에 두지 않는다).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class WeakInternTable
     * @brief 키 하나에 살아 있는 값 하나를 약한 참조로 듭니다. 소유는 받아 간 쪽에 있습니다.
     * @details 나눠 주는 포인터는 값을 감싼 칸 주인(`LiveOwner`)의 별칭 포인터입니다. 마지막 사용자가 놓으면 칸 주인의 소멸자가 표에서 자기 칸을 지우고,
     *          그 뒤(잠금 밖)에 값이 사라집니다 — 값의 소멸자가 같은 표의 다른 값을 놓아도 잠금이 겹치지 않습니다.
     * @tparam KeyType   표의 키입니다(`==` 와 @p KeyHash 로 찾습니다).
     * @tparam ValueType 나눠 줄 값의 타입입니다.
     * @tparam KeyHash   키 해시 함수 객체입니다.
     */
    template <typename KeyType, typename ValueType, typename KeyHash = std::hash<KeyType>>
    class WeakInternTable
    {
        // shared_from_this 는 값을 지은 쪽의 제어 블록을 돌려줘 칸 주인을 살려 두지 않는다 — 칸이 일찍 지워져 같은 키가 두 벌 지어진다.
        static_assert( std::is_base_of_v<std::enable_shared_from_this<ValueType>, ValueType> == false,
                       "WeakInternTable values must not derive from enable_shared_from_this: the shared pointer it hands out aliases a slot owner" );

    public:
        WeakInternTable()
            : _pState{ sw::make_shared<State>() }
        {
        }

        WeakInternTable( const WeakInternTable& )            = delete;
        WeakInternTable& operator=( const WeakInternTable& ) = delete;

        /** @brief 그 키를 지금 쥔 값입니다. 없으면 nullptr 입니다. */
        shared_ptr<ValueType> findLive( const KeyType& key ) const
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            const auto        it = _pState->_mapValue.find( key );
            return ( it != _pState->_mapValue.end() ) ? it->second.lock() : nullptr;
        }

        /**
         * @brief 새로 지은 @p value 를 넣습니다. 그 사이에 다른 쪽이 같은 키로 넣었으면 그것을 돌려주고 @p value 는 버립니다.
         * @details 해시 조회 하나입니다 — 놓인 값은 삭제자가 칸을 지우므로 넣기가 표를 훑지 않습니다.
         */
        shared_ptr<ValueType> insertOrGetLive( const KeyType& key, shared_ptr<ValueType> value )
        {
            ScopedLock<mutex>     lock{ _pState->_mutex };
            weak_ptr<ValueType>&  slot   = _pState->_mapValue[key];
            shared_ptr<ValueType> winner = slot.lock();
            if ( winner == nullptr )
            {
                ValueType* const            pValue = value.get();
                const shared_ptr<LiveOwner> pOwner = sw::make_shared<LiveOwner>( _pState, key, std::move( value ) );
                winner                             = shared_ptr<ValueType>{ pOwner, pValue };
                slot                               = winner;
            }
            return winner;
        }

        /**
         * @brief 그 키의 값을 나눠 받습니다. 처음이면 @p create 로 짓습니다(잠금 밖). 지을 수 없으면(nullptr) nullptr 이고 표에 남기지 않습니다.
         * @details 워커에서 불러도 됩니다(잠급니다).
         */
        template <typename CreateFunction>
        shared_ptr<ValueType> acquire( const KeyType& key, CreateFunction&& create )
        {
            shared_ptr<ValueType> live = findLive( key );
            if ( live != nullptr )
                return live;
            shared_ptr<ValueType> created = create( key );
            if ( created != nullptr )
                live = insertOrGetLive( key, std::move( created ) );
            return live;
        }

        /** @brief 그 키의 칸을 뗍니다. 쥔 쪽의 값은 그대로 살고, 다음 요청은 새로 짓습니다. */
        void erase( const KeyType& key )
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            _pState->_mapValue.erase( key );
        }

        /** @brief 살아 있는 항목 수입니다. */
        size_t countLive() const
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            size_t            liveCount{ 0 };
            for ( const auto& [key, value] : _pState->_mapValue )
            {
                if ( value.expired() == false )
                    ++liveCount;
            }
            return liveCount;
        }

        /** @brief 칸 수입니다(만료 칸 포함). 놓인 값은 삭제자가 지우므로 `countLive` 와 같아야 합니다 — 테스트가 봅니다. */
        size_t countSlots() const
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            return _pState->_mapValue.size();
        }

        /** @brief 표를 비우고 버킷까지 돌려줍니다(종료 누수 검사에 표의 몫이 남지 않게). 쥔 쪽의 값은 그대로 살고, 다음 요청은 새로 짓습니다. */
        void clear()
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            _pState->_mapValue = unordered_map<KeyType, weak_ptr<ValueType>, KeyHash>{};
        }

    private:
        /** @brief 잠금과 칸들입니다. 칸 주인이 약한 참조로 들어, 표가 먼저 사라져도 값을 놓을 때 지울 곳이 없을 뿐입니다. */
        struct State
        {
            mutable mutex                                                  _mutex;
            unordered_map<KeyType, weak_ptr<ValueType>, KeyHash> _mapValue SW_GUARDED_BY( _mutex );
        };

        /** @brief 나눠 준 값 하나를 소유하는 칸 주인입니다. 마지막 사용자가 놓으면 소멸자가 자기 칸을 지웁니다. */
        struct LiveOwner
        {
            weak_ptr<State>       _pState;
            KeyType               _key;
            shared_ptr<ValueType> _value;

            LiveOwner( const shared_ptr<State>& pState, const KeyType& key, shared_ptr<ValueType> value )
                : _pState{ pState }
                , _key{ key }
                , _value{ std::move( value ) }
            {
            }

            LiveOwner( const LiveOwner& )            = delete;
            LiveOwner& operator=( const LiveOwner& ) = delete;

            ~LiveOwner()
            {
                const shared_ptr<State> pState = _pState.lock();
                if ( pState == nullptr )
                    return;
                ScopedLock<mutex> lock{ pState->_mutex };
                const auto        it = pState->_mapValue.find( _key );
                // 그 사이 `erase` 뒤에 같은 키로 새 값이 들었으면 그 칸은 살아 있다 — 만료된 칸(곧 이 주인의 칸)만 지운다.
                if ( it != pState->_mapValue.end() && it->second.expired() )
                    pState->_mapValue.erase( it );
            } // 잠금을 푼 뒤에 `_value` 가 사라진다(멤버 소멸은 본문 뒤)
        };

        shared_ptr<State> _pState;
    };
} // namespace sw
