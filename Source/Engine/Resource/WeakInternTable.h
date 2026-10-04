/**
 * @file WeakInternTable.h
 * @brief 키 → 약한 참조 값 표 하나입니다. "같은 키면 같은 객체를 나눠 주고, 마지막 사용자가 놓으면 사라지는" 캐시가 모두 이 구현을 씁니다.
 * @details 경로로 읽는 에셋 표(`SharedAssetTable` — 스켈레톤 · 클립 · 리그 · 스프라이트 클립 · 캐릭터 데이터)와 코드로 짓는 값 표(`WeakInternCache` —
 *          내장 도형 · 9-슬라이스 메시 · 스프라이트 텍스처 인스턴스)가 같은 표를 씁니다. 값을 짓는 일(파일 IO · 메시 생성)은 잠금 밖에서 하고,
 *          둘이 같은 키를 동시에 지으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받습니다. 사라진 칸은 새 칸을 넣을 때 걷습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class WeakInternTable
     * @brief 키 하나에 살아 있는 값 하나를 약한 참조로 듭니다. 소유는 받아 간 쪽에 있습니다.
     * @tparam KeyType   표의 키입니다(`==` 와 @p KeyHash 로 찾습니다).
     * @tparam ValueType 나눠 줄 값의 타입입니다.
     * @tparam KeyHash   키 해시 함수 객체입니다.
     */
    template <typename KeyType, typename ValueType, typename KeyHash = std::hash<KeyType>>
    class WeakInternTable
    {
    public:
        WeakInternTable()
            : _mutex{}
            , _mapValue{}
        {
        }

        WeakInternTable( const WeakInternTable& )            = delete;
        WeakInternTable& operator=( const WeakInternTable& ) = delete;

        /** @brief 그 키를 지금 쥔 값입니다. 없으면 nullptr 입니다. */
        shared_ptr<ValueType> findLive( const KeyType& key ) const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            const auto              it = _mapValue.find( key );
            return ( it != _mapValue.end() ) ? it->second.lock() : nullptr;
        }

        /**
         * @brief 새로 지은 @p value 를 넣습니다. 그 사이에 다른 쪽이 같은 키로 넣었으면 그것을 돌려주고 @p value 는 버립니다.
         * @details 넣기 전에 사라진 칸을 걷습니다 — 크기를 끌어 바꾸는 편집처럼 키가 계속 바뀌는 쪽이 칸을 쌓지 않습니다.
         */
        shared_ptr<ValueType> insertOrGetLive( const KeyType& key, shared_ptr<ValueType> value )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( auto iter = _mapValue.begin(); iter != _mapValue.end(); )
            {
                if ( iter->second.expired() )
                    iter = _mapValue.erase( iter );
                else
                    ++iter;
            }
            weak_ptr<ValueType>&  slot   = _mapValue[key];
            shared_ptr<ValueType> winner = slot.lock();
            if ( winner != nullptr )
                return winner;
            slot = value;
            return value;
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
            if ( created == nullptr )
                return nullptr;
            return insertOrGetLive( key, std::move( created ) );
        }

        /** @brief 그 키의 칸을 뗍니다. 쥔 쪽의 값은 그대로 살고, 다음 요청은 새로 짓습니다. */
        void erase( const KeyType& key )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _mapValue.erase( key );
        }

        /** @brief 살아 있는 항목 수입니다. */
        size_t countLive() const
        {
            std::scoped_lock<mutex> lock{ _mutex };
            size_t                  liveCount{ 0 };
            for ( const auto& [key, value] : _mapValue )
            {
                if ( value.expired() == false )
                    ++liveCount;
            }
            return liveCount;
        }

        /** @brief 표를 비우고 버킷까지 돌려줍니다(종료 누수 검사에 표의 몫이 남지 않게). 쥔 쪽의 값은 그대로 살고, 다음 요청은 새로 짓습니다. */
        void clear()
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _mapValue = unordered_map<KeyType, weak_ptr<ValueType>, KeyHash>{};
        }

    private:
        mutable mutex                                        _mutex;
        unordered_map<KeyType, weak_ptr<ValueType>, KeyHash> _mapValue;
    };
} // namespace sw
