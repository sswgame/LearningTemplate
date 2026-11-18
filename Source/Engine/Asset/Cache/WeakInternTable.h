/**
 * @file WeakInternTable.h
 * @brief 키 → 약한 참조 값 표 하나입니다. "같은 키면 같은 객체를 나눠 주고, 마지막 사용자가 놓으면 사라지는" 캐시가 모두 이 구현을 씁니다.
 * @details 경로로 읽는 에셋 표(`SharedAssetTable` — 스켈레톤 · 클립 · 리그 · 스프라이트 클립 · 캐릭터 데이터)와 코드로 짓는 값 표(`WeakInternCache` —
 *          내장 도형 · 9-슬라이스 메시 · 스프라이트 텍스처 인스턴스)가 같은 표를 씁니다. 값을 짓는 일(파일 IO · 메시 생성)은 잠금 밖에서 하고,
 *          둘이 같은 키를 동시에 지으면 먼저 넣은 쪽이 남고 다른 쪽은 그것을 받습니다. 마지막 사용자가 놓으면 그 값의 삭제자가 자기 칸을 지웁니다 —
 *          표에 만료 칸이 쌓이지 않으므로 넣기 · 찾기는 해시 조회 하나입니다(언리얼 · 유니티의 에셋 표도 해시 조회이고, 표 전체를 훑는 수거를 넣기 길에 두지 않는다).
 *
 *          [판 사슬] 다시 읽기는 쥔 값을 바꾸지 않고 새 값을 새 판으로 넣는다(`replaceLive`). 옛 판은 새 판을 가리키고(`WeakInternVersion::_pNext`) 그 새 판을 소유한다 —
 *          옛 판을 쥔 따라가는 참조(`SharedAssetRef`)는 읽을 때 사슬 끝의 새 판을 본다. 언리얼이 다시 읽은 에셋의 참조를 새 객체로 바꾸는 일(핫 리로드 참조 교체)을
 *          참조 쪽 쓰기 없이 하는 모양이다. 옛 판은 쥔 쪽이 있는 동안 살아 있으므로 거기서 꺼낸 날 포인터도 끊기지 않는다.
 *
 *          [사슬 정리] 따라가는 참조는 표의 따라가기 목록(`WeakInternFollowerList`)에 오른다. 다시 읽은 직후(게임 스레드, 틱 밖) `followRefs` 한 입구가 목록의
 *          참조를 모두 최신 판으로 옮겨 사슬이 다시 읽기 횟수만큼 자라지 않게 한다. 옮기며 놓은 옛 판은 프레임 경계 두 번(`collectRetired`)을 지나야 놓는다 —
 *          그 사이 다른 스레드가 옛 판을 읽던 중이어도, 날 포인터를 빌린 쪽이 한 틱 안에 다시 짓기 전이어도 끊기지 않는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    template <typename ValueType>
    struct WeakInternFollowerList;

    /**
     * @struct WeakInternVersion
     * @brief 표가 나눠 준 값 한 판입니다. 다시 읽기가 새 판을 넣으면 `_pNext` 가 새 판을 가리킵니다(한 번만 적는다).
     * @details `_pNextValue` 는 새 판을 살려 두는 소유이고 `_pNext` 를 적기 **전에** 한 번 적는다 — 읽는 쪽은 `_pNext` 만 원자로 읽는다.
     *          `_pSelf` · `_pFollowers` 는 판을 지을 때(`makeLive`) 한 번 적고 바꾸지 않는다.
     */
    template <typename ValueType>
    struct WeakInternVersion
    {
        ValueType*                                    _pValue{ nullptr }; ///< 이 판의 값(칸 주인이 소유한다)
        weak_ptr<ValueType>                           _pSelf;             ///< 이 판의 나눠 준 포인터(스냅숏 · 참조 옮기기가 소유를 얻는다)
        shared_ptr<WeakInternFollowerList<ValueType>> _pFollowers;        ///< 이 판을 낸 표의 따라가기 목록(표보다 오래 살 수 있다)
        shared_ptr<ValueType>                         _pNextValue;        ///< 다음 판의 나눠 준 포인터(다음 판을 살려 둔다). `_pNext` 를 적기 전에 한 번만 적는다
        atomic<const WeakInternVersion*>              _pNext{ nullptr };  ///< 다음 판. 없으면 이 판이 최신이다

        /** @brief 사슬 끝(최신 판)입니다. 쓰기 없이 원자 읽기만 합니다. */
        const WeakInternVersion* resolveLatestVersion() const
        {
            const WeakInternVersion* pVersion = this;
            for ( const WeakInternVersion* pNext = pVersion->_pNext.load( std::memory_order_acquire ); pNext != nullptr; pNext = pVersion->_pNext.load( std::memory_order_acquire ) )
            {
                pVersion = pNext;
            }
            return pVersion;
        }
        /** @brief 사슬 끝(최신 판)의 값입니다. */
        ValueType* resolveLatest() const { return resolveLatestVersion()->_pValue; }
    };

    /**
     * @struct WeakInternFollower
     * @brief 따라가는 참조(`SharedAssetRef`) 하나의 마디입니다. 표의 따라가기 목록에 올라 있는 동안 `_pHeld` 는 원자 함수(`std::atomic_load` · `std::atomic_exchange`)로만 다룬다.
     */
    template <typename ValueType>
    struct WeakInternFollower
    {
        atomic<const WeakInternVersion<ValueType>*> _pVersion{ nullptr }; ///< 쥔 판(머리). 정리가 최신 판으로 옮긴다
        shared_ptr<ValueType>                       _pHeld;               ///< 쥔 판의 소유
        WeakInternFollower*                         _pPrev{ nullptr };    ///< 목록의 앞(목록의 잠금 안에서만)
        WeakInternFollower*                         _pNext{ nullptr };    ///< 목록의 뒤(목록의 잠금 안에서만)
    };

    /**
     * @struct WeakInternFollowerList
     * @brief 한 표가 나눠 준 값을 따라가는 참조들의 목록입니다. 정리(`followAll`)는 게임 스레드, 틱 밖에서만 부른다.
     */
    template <typename ValueType>
    struct WeakInternFollowerList
    {
        mutable mutex                                     _mutex;
        WeakInternFollower<ValueType>* _pHead             SW_GUARDED_BY( _mutex ){ nullptr };
        vector<shared_ptr<ValueType>> _listRetired        SW_GUARDED_BY( _mutex ); ///< 이번 프레임의 정리가 놓은 옛 판
        vector<shared_ptr<ValueType>> _listRetiredEarlier SW_GUARDED_BY( _mutex ); ///< 지난 프레임의 정리가 놓은 옛 판(다음 프레임 경계에 놓는다)
        atomic<bool>                                      _bHasRetired{ false };   ///< 남긴 옛 판이 있다(프레임 경계가 잠그지 않고 건너뛴다)

        void add( WeakInternFollower<ValueType>& follower )
        {
            ScopedLock<mutex> lock{ _mutex };
            follower._pPrev = nullptr;
            follower._pNext = _pHead;
            if ( _pHead != nullptr )
                _pHead->_pPrev = &follower;
            _pHead = &follower;
        }

        void remove( WeakInternFollower<ValueType>& follower )
        {
            ScopedLock<mutex> lock{ _mutex };
            if ( follower._pPrev != nullptr )
                follower._pPrev->_pNext = follower._pNext;
            else
                _pHead = follower._pNext;
            if ( follower._pNext != nullptr )
                follower._pNext->_pPrev = follower._pPrev;
            follower._pPrev = nullptr;
            follower._pNext = nullptr;
        }

        /**
         * @brief 목록의 참조를 모두 최신 판으로 옮기고 옮긴 수를 돌려줍니다(다시 읽은 직후 — 게임 스레드, 틱 밖). 놓은 옛 판은 `collectRetired` 두 번 뒤에 놓는다.
         * @details 참조의 판은 원자로 바꾸므로 그 참조를 다른 스레드가 읽던 중이어도 된다.
         */
        uint32 followAll()
        {
            uint32            movedCount = 0;
            ScopedLock<mutex> lock{ _mutex };
            for ( WeakInternFollower<ValueType>* pFollower = _pHead; pFollower != nullptr; pFollower = pFollower->_pNext )
            {
                const WeakInternVersion<ValueType>* const pHead   = pFollower->_pVersion.load( std::memory_order_acquire );
                const WeakInternVersion<ValueType>* const pLatest = pHead != nullptr ? pHead->resolveLatestVersion() : nullptr;
                if ( pLatest == pHead )
                    continue;
                // 최신 판은 앞 판의 `_pNextValue` 가 살려 두므로 잠글 수 있다.
                shared_ptr<ValueType> pOld = std::atomic_exchange( &pFollower->_pHeld, pLatest->_pSelf.lock() );
                pFollower->_pVersion.store( pLatest, std::memory_order_release );
                _listRetired.push_back( std::move( pOld ) );
                ++movedCount;
            }
            if ( movedCount > 0 )
                _bHasRetired.store( true, std::memory_order_release );
            return movedCount;
        }

        /**
         * @brief 프레임 경계(게임 스레드, 틱 밖)에서 부릅니다. 지지난 정리의 옛 판을 놓고 지난 정리의 옛 판은 한 프레임 더 남깁니다. 놓은 수를 돌려줍니다.
         * @details 옛 판을 날 포인터로 빌린 쪽(애니메이터의 플레이어 등)은 그 사이의 틱에서 새 판으로 다시 짓는다.
         */
        size_t collectRetired()
        {
            if ( _bHasRetired.load( std::memory_order_acquire ) == false )
                return 0;
            vector<shared_ptr<ValueType>> listRelease; // 잠금보다 오래 산다 — 놓는 옛 판의 칸 주인이 표를 잠근다
            ScopedLock<mutex>             lock{ _mutex };
            listRelease.swap( _listRetiredEarlier );
            _listRetiredEarlier.swap( _listRetired );
            if ( _listRetiredEarlier.empty() )
                _bHasRetired.store( false, std::memory_order_release );
            return listRelease.size();
        }

        /** @brief 남긴 옛 판을 모두 놓습니다(표를 비우거나 지울 때). 옛 판이 이 목록을 쥐어(`_pFollowers`) 서로 붙든 고리를 끊는다. */
        void releaseRetired()
        {
            vector<shared_ptr<ValueType>> listRelease; // 잠금보다 오래 산다 — 놓는 옛 판의 칸 주인이 표를 잠근다
            vector<shared_ptr<ValueType>> listReleaseEarlier;
            ScopedLock<mutex>             lock{ _mutex };
            listRelease.swap( _listRetired );
            listReleaseEarlier.swap( _listRetiredEarlier );
            _bHasRetired.store( false, std::memory_order_release );
        }
    };

    /**
     * @class WeakInternTable
     * @brief 키 하나에 살아 있는 값 하나를 약한 참조로 듭니다. 소유는 받아 간 쪽에 있습니다.
     * @details 나눠 주는 포인터는 값을 감싼 칸 주인(`LiveOwner`, 값의 판)의 별칭 포인터입니다. 마지막 사용자가 놓으면 칸 주인의 소멸자가 표에서 자기 칸을 지우고,
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
                       "WeakInternTable values must not derive from enable_shared_from_this: the shared pointer it hands out is owned through a slot owner" );

    public:
        WeakInternTable()
            : _pState{ sw::make_shared<State>() }
            , _pFollowers{ sw::make_shared<WeakInternFollowerList<ValueType>>() }
        {
        }

        ~WeakInternTable() { _pFollowers->releaseRetired(); }

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
                winner = makeLive( key, std::move( value ) );
                slot   = winner;
            }
            return winner;
        }

        /**
         * @brief 그 키를 쥔 값이 있으면 @p value 를 새 판으로 넣고 옛 판이 새 판을 가리키게 합니다. 쥔 값이 없으면 넣지 않고 false 입니다.
         * @details 쥔 값은 바뀌지 않는다 — 옛 포인터를 쥔 쪽은 옛 값을 그대로 읽고, 따라가는 참조(`SharedAssetRef`)는 새 값을 본다. 다음 찾기 · 넣기는 새 값을 받는다.
         *          옛 판이 새 판을 소유하므로 따라가는 참조만 남아도 새 값이 산다. 게임 스레드, 틱 밖에서 부른다(따라가는 참조가 틱 중에 바뀐 값을 보지 않게).
         */
        [[nodiscard]] bool replaceLive( const KeyType& key, shared_ptr<ValueType> value )
        {
            shared_ptr<ValueType> old; // 잠금보다 오래 산다 — 마지막 사용자가 그 사이 놓았으면 칸 주인의 소멸자가 같은 잠금을 잡는다
            ScopedLock<mutex>     lock{ _pState->_mutex };
            const auto            it = _pState->_mapValue.find( key );
            if ( it == _pState->_mapValue.end() )
                return false;
            old = it->second.lock();
            if ( old == nullptr )
                return false;
            WeakInternVersion<ValueType>* const pOldVersion = _pState->_mapVersion[old.get()]; // 칸의 값은 늘 판 표에 있다(`makeLive`)
            const shared_ptr<ValueType>         fresh       = makeLive( key, std::move( value ) );
            it->second                                      = fresh;
            // 칸의 값은 늘 최신 판이다 — 옛 판에는 아직 다음 판이 없다. 소유를 먼저 적고 원자 포인터로 알린다.
            pOldVersion->_pNextValue = fresh;
            pOldVersion->_pNext.store( _pState->_mapVersion[fresh.get()], std::memory_order_release );
            return true;
        }

        /**
         * @brief 이 표가 나눠 준 @p pValue 의 판입니다. 이 표에서 오지 않은 포인터면 nullptr 입니다. 워커에서 불러도 됩니다(잠급니다).
         * @details 판은 @p pValue 가 사는 동안 산다(칸 주인이 판이다). 따라가는 참조(`SharedAssetRef`)가 받을 때 한 번 찾는다.
         */
        WeakInternVersion<ValueType>* findVersion( const shared_ptr<ValueType>& pValue ) const
        {
            if ( pValue == nullptr )
                return nullptr;
            ScopedLock<mutex> lock{ _pState->_mutex };
            const auto        it = _pState->_mapVersion.find( pValue.get() );
            return it != _pState->_mapVersion.end() ? it->second : nullptr;
        }

        /**
         * @brief 따라가는 참조를 모두 최신 판으로 옮깁니다(사슬 정리 — 다시 읽은 직후 · 게임 스레드, 틱 밖). 옮긴 수를 돌려줍니다.
         * @details 옮기며 놓은 옛 판은 다음 정리까지 남는다(다른 스레드의 읽기를 위한 한 번의 유예).
         */
        uint32 followRefs() { return _pFollowers->followAll(); }
        /** @brief 프레임 경계에서 정리가 남긴 옛 판을 놓습니다(`WeakInternFollowerList::collectRetired`). */
        size_t collectRetired() { return _pFollowers->collectRetired(); }

        /** @brief 살아 있는 판 수입니다(옛 판 포함 — 사슬 정리를 테스트가 본다). */
        size_t countVersions() const
        {
            ScopedLock<mutex> lock{ _pState->_mutex };
            return _pState->_mapVersion.size();
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
            _pFollowers->releaseRetired(); // 표의 잠금 밖 — 놓는 옛 판의 칸 주인이 표를 잠근다
            ScopedLock<mutex> lock{ _pState->_mutex };
            _pState->_mapValue = unordered_map<KeyType, weak_ptr<ValueType>, KeyHash>{};
        }

    private:
        /** @brief @p value 를 감싼 칸 주인을 짓고 판 표에 올린 뒤 나눠 줄 별칭 포인터를 만듭니다. 잠금 안에서 부른다. */
        shared_ptr<ValueType> makeLive( const KeyType& key, shared_ptr<ValueType> value ) const SW_REQUIRES_LOCK( _pState->_mutex )
        {
            ValueType* const            pValue = value.get();
            const shared_ptr<LiveOwner> pOwner = sw::make_shared<LiveOwner>( _pState, key, std::move( value ) );
            _pState->_mapVersion[pValue]       = pOwner.get();
            shared_ptr<ValueType> live{ pOwner, pValue };
            pOwner->_pSelf      = live;
            pOwner->_pFollowers = _pFollowers;
            return live;
        }

    private:
        /** @brief 잠금과 칸들입니다. 칸 주인이 약한 참조로 들어, 표가 먼저 사라져도 값을 놓을 때 지울 곳이 없을 뿐입니다. */
        struct State
        {
            mutable mutex                                                         _mutex;
            unordered_map<KeyType, weak_ptr<ValueType>, KeyHash> _mapValue        SW_GUARDED_BY( _mutex );
            unordered_map<const void*, WeakInternVersion<ValueType>*> _mapVersion SW_GUARDED_BY( _mutex ); ///< 값 주소 → 판(칸 주인). `clear` 뒤에도 산 값은 남고 칸 주인이 지운다
        };

        /** @brief 나눠 준 값 하나(한 판)를 소유하는 칸 주인입니다. 마지막 사용자가 놓으면 소멸자가 자기 칸을 지웁니다. */
        struct LiveOwner : WeakInternVersion<ValueType>
        {
            weak_ptr<State>       _pState;
            KeyType               _key;
            shared_ptr<ValueType> _value;

            LiveOwner( const shared_ptr<State>& pState, const KeyType& key, shared_ptr<ValueType> value )
                : _pState{ pState }
                , _key{ key }
                , _value{ std::move( value ) }
            {
                this->_pValue = _value.get();
            }

            LiveOwner( const LiveOwner& )            = delete;
            LiveOwner& operator=( const LiveOwner& ) = delete;

            ~LiveOwner()
            {
                const shared_ptr<State> pState = _pState.lock();
                if ( pState == nullptr )
                    return;
                ScopedLock<mutex> lock{ pState->_mutex };
                pState->_mapVersion.erase( this->_pValue );
                const auto it = pState->_mapValue.find( _key );
                // 그 사이 `erase` 뒤에 같은 키로 새 값이 들었으면 그 칸은 살아 있다 — 만료된 칸(곧 이 주인의 칸)만 지운다.
                if ( it != pState->_mapValue.end() && it->second.expired() )
                    pState->_mapValue.erase( it );
            } // 잠금을 푼 뒤에 `_value` 가 사라진다(멤버 소멸은 본문 뒤)
        };

        shared_ptr<State>                             _pState;
        shared_ptr<WeakInternFollowerList<ValueType>> _pFollowers; ///< 따라가는 참조 목록(판이 함께 쥐어 표보다 오래 살 수 있다)
    };
} // namespace sw
