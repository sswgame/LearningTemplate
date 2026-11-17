/**
 * @file SharedAssetRef.h
 * @brief 공유 캐시가 나눠 준 에셋을 쥐고, 다시 읽히면 읽을 때 새 판을 보는 참조 형식입니다(언리얼 핫 리로드의 참조 교체 · 유니티 에셋 참조 갱신 자리).
 * @details 공유 캐시(`SharedAssetCache`)는 쥔 객체를 제자리에서 바꾸지 않는다(A-7 — 워커가 읽는 중에 내용이 덮이지 않게). 다시 읽기는 새 판을 넣고 옛 판이
 *          새 판을 가리키게 한다(`WeakInternTable::replaceLive`). @p T 는 공유 캐시가 있는 종류다(`SW_DECLARE_SHARED_ASSET_CACHE`). 이 참조는 받은 판을 쥐고
 *          `get` 때 사슬 끝을 원자로 읽어 돌려준다 — 참조 쪽에 쓰기가 없어 틱 안 · 워커 어디서 읽어도 된다. 그래서 멤버로 쥔 쪽이 손으로 다시 받지 않아도 새 내용을 본다.
 *          사슬은 게임 스레드가 틱 밖에서만 늘리므로 한 틱 안에서 `get` 의 결과는 바뀌지 않는다. 다시 읽은 직후 캐시가 표의 따라가기 목록에 오른 참조를 모두
 *          최신 판으로 옮겨(`SharedAssetTable::followRefs`) 사슬이 다시 읽기 횟수만큼 자라지 않는다.
 *
 *          공유 캐시의 `acquire` 는 이 형식을 돌려준다. `shared_ptr<const T>` 와 서로 저절로 바뀌지 않는다 — 받은 때의 판을 그대로 쥐어야 하는 쪽(번호 · 별칭 포인터를
 *          지어 둔 쪽, 워커 일감)은 `snapshot()` 으로 이름을 대고 받는다. 그래서 멤버를 `shared_ptr` 로 쥐면서 다시 읽기를 놓치는 모양은 컴파일이 막는다.
 *          옛 판은 이 참조가 쥔 동안(옮긴 뒤에는 다음 정리까지) 살아 있으므로 `get` 으로 꺼낸 날 포인터가 다시 읽기 때문에 곧바로 끊기지는 않는다 —
 *          그래도 날 포인터를 멤버로 들지 말고 쓸 때마다 `get` 으로 꺼낸다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/WeakInternTable.h"

#include <cstddef>

namespace sw
{
    template <typename AssetType>
    class SharedAssetCache;

    /**
     * @class SharedAssetRef
     * @brief 공유 에셋 @p T 한 벌을 쥐고 최신 판을 읽습니다. 표에서 오지 않은 객체(테스트가 지은 값)는 그 객체 그대로입니다.
     */
    template <typename T>
    class SharedAssetRef
    {
    public:
        SharedAssetRef() = default;
        SharedAssetRef( std::nullptr_t ) {}
        /**
         * @brief 공유 캐시가 나눠 준 포인터를 쥡니다. 다른 곳에서 지은 객체면 따라갈 판이 없다. 암묵 변환이 아니다 — 스냅숏을 참조로 바꾸는 자리는 이름을 댄다.
         * @details 판은 그 캐시의 표에서 한 번 찾는다(`SharedAssetCache<T>::findVersion`, 잠근다) — 받을 때만이고 읽기(`get`)는 잠그지 않는다.
         *          RTTI 를 끈 구성이라 삭제자(`std::get_deleter`)로 찾지 않는다.
         */
        explicit SharedAssetRef( shared_ptr<const T> pAsset )
        {
            const WeakInternVersion<const T>* const pVersion = SharedAssetCache<T>::findVersion( pAsset );
            attach( std::move( pAsset ), pVersion );
        }
        /** @brief 판을 이미 아는 쪽(캐시 · 표)이 씁니다. @p pVersion 은 @p pAsset 의 판이어야 한다(nullptr 면 따라가지 않는다). */
        SharedAssetRef( shared_ptr<const T> pAsset, const WeakInternVersion<const T>* pVersion ) { attach( std::move( pAsset ), pVersion ); }

        /**
         * @brief 소유하지 않고 따라가지 않는 참조입니다 — 공유 캐시 밖에서 지은 값(테스트 · 쥔 쪽이 수명을 지키는 지역 값)을 이 형식을 받는 쪽에 넘길 때만 쓴다.
         * @details @p pAsset 은 이 참조보다 오래 살아야 한다. 공유 캐시가 준 값은 `acquire` 의 참조를 그대로 넘긴다(빌리면 다시 읽기를 따라가지 않는다).
         */
        [[nodiscard]] static SharedAssetRef borrow( const T* pAsset )
        {
            SharedAssetRef ref;
            ref._pForeign = pAsset;
            return ref;
        }

        SharedAssetRef( const SharedAssetRef& other ) { copyFrom( other ); }
        SharedAssetRef( SharedAssetRef&& other ) noexcept
        {
            copyFrom( other );
            other.reset();
        }
        SharedAssetRef& operator=( const SharedAssetRef& other )
        {
            if ( this != &other )
            {
                reset();
                copyFrom( other );
            }
            return *this;
        }
        SharedAssetRef& operator=( SharedAssetRef&& other ) noexcept
        {
            if ( this != &other )
            {
                reset();
                copyFrom( other );
                other.reset();
            }
            return *this;
        }
        SharedAssetRef& operator=( std::nullptr_t )
        {
            reset();
            return *this;
        }
        ~SharedAssetRef() { reset(); }

        /** @brief 최신 판의 에셋입니다. 비었으면 nullptr 입니다. 쓰기 없이 원자 읽기만 합니다. */
        const T* get() const
        {
            const WeakInternVersion<const T>* const pVersion = _node._pVersion.load( std::memory_order_acquire );
            return pVersion != nullptr ? pVersion->resolveLatest() : _pForeign;
        }
        const T* operator->() const { return get(); }
        const T& operator*() const { return *get(); }
        explicit operator bool() const { return get() != nullptr; }

        /**
         * @brief 최신 판을 소유 포인터로 돌려줍니다. 받은 쪽은 그 판을 스냅숏으로 쥔다(워커 일감, 번호를 지어 둔 쪽, `shared_ptr` 를 받는 함수).
         * @details 최신 판은 앞 판의 `_pNextValue`(머리면 이 참조)가 살려 두므로 `_pSelf` 를 잠글 수 있다.
         */
        shared_ptr<const T> snapshot() const
        {
            const WeakInternVersion<const T>* const pVersion = _node._pVersion.load( std::memory_order_acquire );
            if ( pVersion == nullptr && _pForeign != nullptr )
                return shared_ptr<const T>( shared_ptr<const T>{}, _pForeign ); // 빌린 값 — 소유하지 않는 별칭이다(수명은 빌려준 쪽이 지킨다, `borrow`). 빼면 빌린 참조의 스냅숏이 비어 워커 일감이 null 을 읽었다
            if ( pVersion == nullptr )
                return _node._pHeld; // 따라가지 않는 객체 — 목록에 오르지 않아 원자로 다룰 까닭이 없다
            return pVersion->resolveLatestVersion()->_pSelf.lock();
        }

        /** @brief 놓습니다. */
        void reset()
        {
            if ( _pFollowers != nullptr )
            {
                _pFollowers->remove( _node );
                _pFollowers.reset();
            }
            _node._pVersion.store( nullptr, std::memory_order_release );
            _node._pHeld.reset();
            _pForeign = nullptr;
        }

        friend bool operator==( const SharedAssetRef& lhs, const SharedAssetRef& rhs ) { return lhs.get() == rhs.get(); }
        friend bool operator!=( const SharedAssetRef& lhs, const SharedAssetRef& rhs ) { return lhs.get() != rhs.get(); }
        friend bool operator==( const SharedAssetRef& lhs, std::nullptr_t ) { return lhs.get() == nullptr; }
        friend bool operator!=( const SharedAssetRef& lhs, std::nullptr_t ) { return lhs.get() != nullptr; }
        friend bool operator==( std::nullptr_t, const SharedAssetRef& rhs ) { return rhs.get() == nullptr; }
        friend bool operator!=( std::nullptr_t, const SharedAssetRef& rhs ) { return rhs.get() != nullptr; }

    private:
        /** @brief 빈 참조에 @p pAsset 을 쥡니다. 판이 있으면 표의 따라가기 목록에 오른다. */
        void attach( shared_ptr<const T> pAsset, const WeakInternVersion<const T>* pVersion )
        {
            if ( pVersion == nullptr )
            {
                _pForeign    = pAsset.get();
                _node._pHeld = std::move( pAsset );
                return;
            }
            _node._pHeld = std::move( pAsset );
            _node._pVersion.store( pVersion, std::memory_order_release );
            _pFollowers = pVersion->_pFollowers;
            _pFollowers->add( _node );
        }

        /** @brief 빈 참조에 @p other 의 최신 판을 쥡니다(다른 스레드의 정리와 겹쳐도 최신 판의 소유부터 얻는다). */
        void copyFrom( const SharedAssetRef& other )
        {
            const WeakInternVersion<const T>* const pVersion = other._node._pVersion.load( std::memory_order_acquire );
            if ( pVersion == nullptr )
            {
                _pForeign    = other._pForeign;
                _node._pHeld = other._node._pHeld;
                return;
            }
            const WeakInternVersion<const T>* const pLatest = pVersion->resolveLatestVersion();
            attach( pLatest->_pSelf.lock(), pLatest );
        }

    private:
        WeakInternFollower<const T>                 _node;                ///< 쥔 판과 소유(따라가기 목록의 마디)
        shared_ptr<WeakInternFollowerList<const T>> _pFollowers;          ///< 오른 따라가기 목록. 따라가지 않으면 nullptr
        const T*                                    _pForeign{ nullptr }; ///< 표에서 오지 않은 객체(따라가지 않는다)
    };
} // namespace sw
