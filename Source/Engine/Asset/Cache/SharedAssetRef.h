/**
 * @file SharedAssetRef.h
 * @brief 공유 캐시가 나눠 준 에셋을 쥐고, 다시 읽히면 읽을 때 새 판을 보는 참조 형식입니다(언리얼 핫 리로드의 참조 교체 · 유니티 에셋 참조 갱신 자리).
 * @details 공유 캐시(`SharedAssetCache`)는 쥔 객체를 제자리에서 바꾸지 않는다(A-7 — 워커가 읽는 중에 내용이 덮이지 않게). 다시 읽기는 새 판을 넣고 옛 판이
 *          새 판을 가리키게 한다(`WeakInternTable::replaceLive`). @p T 는 공유 캐시가 있는 종류다(`SW_DECLARE_SHARED_ASSET_CACHE`). 이 참조는 처음 받은 판을 쥐고 `get` 때 사슬 끝을 원자로 읽어 돌려준다 — 참조 쪽에 쓰기가 없어
 *          틱 안 · 워커 어디서 읽어도 된다. 그래서 멤버로 쥔 쪽이 손으로 다시 받지 않아도(감시를 빠뜨려도) 다시 읽은 내용을 본다.
 *          사슬은 게임 스레드가 틱 밖에서만 늘리므로 한 틱 안에서 `get` 의 결과는 바뀌지 않는다.
 *
 *          `shared_ptr<const T>` 를 그대로 쥐는 곳은 받은 때의 판(스냅숏)을 본다 — 번호 · 별칭 포인터를 지어 둔 쪽(타일셋 · 효과 · 감독 프로필 · 상호작용 표)은
 *          일부러 그렇게 쥐고 `AssetReloadWatch` 로 다시 받는다. 멤버로 오래 쥐면서 다시 읽은 내용을 봐야 하는 곳은 이 형식을 쓴다.
 *          옛 판은 이 참조가 사는 동안 살아 있으므로 `get` 으로 꺼낸 날 포인터가 다시 읽기 때문에 끊기지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/WeakInternTable.h"

#include <cstddef>
#include <type_traits>

namespace sw
{
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
         * @brief 공유 캐시가 나눠 준 포인터(`SharedAssetCache<T>::acquire`)를 쥡니다. 다른 곳에서 지은 객체면 따라갈 판이 없다.
         * @details 판은 그 캐시의 표에서 한 번 찾는다(`SharedAssetCache<T>::findVersion`, 잠근다) — 받을 때만이고 읽기(`get`)는 잠그지 않는다.
         *          RTTI 를 끈 구성이라 삭제자(`std::get_deleter`)로 찾지 않는다.
         */
        SharedAssetRef( shared_ptr<const T> pAsset )
            : _pHeld{ std::move( pAsset ) }
            , _pVersion{ SharedAssetCache<T>::findVersion( _pHeld ) }
        {
        }
        /** @brief 판을 이미 아는 쪽(표 · 사슬 옮기기)이 씁니다. @p pVersion 은 @p pAsset 의 판이어야 한다. */
        SharedAssetRef( shared_ptr<const T> pAsset, const WeakInternVersion<const T>* pVersion )
            : _pHeld{ std::move( pAsset ) }
            , _pVersion{ pVersion }
        {
        }
        /** @brief `shared_ptr<T>`(const 아님)처럼 `const T` 로 바뀌는 포인터도 받습니다. */
        template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, const T*> && std::is_same_v<U, const T> == false>>
        SharedAssetRef( shared_ptr<U> pAsset )
            : SharedAssetRef{ shared_ptr<const T>{ std::move( pAsset ) } }
        {
        }

        /** @brief 최신 판의 에셋입니다. 비었으면 nullptr 입니다. 쓰기 없이 원자 읽기만 합니다. */
        const T* get() const { return _pVersion != nullptr ? _pVersion->resolveLatest() : _pHeld.get(); }
        const T* operator->() const { return get(); }
        const T& operator*() const { return *get(); }
        explicit operator bool() const { return _pHeld != nullptr; }

        /**
         * @brief 최신 판을 소유 포인터로 돌려줍니다. 받은 쪽은 그 판을 스냅숏으로 쥔다(워커에 넘기거나 `shared_ptr` 를 받는 함수에 줄 때).
         * @details 다음 판의 소유(`_pNextValue`)는 `_pNext` 를 알리기 전에 한 번만 적히므로 원자 읽기 뒤에 읽어도 된다.
         */
        shared_ptr<const T> getShared() const
        {
            const shared_ptr<const T>*        pLatest  = &_pHeld;
            const WeakInternVersion<const T>* pVersion = _pVersion;
            for ( const WeakInternVersion<const T>* pNext = pVersion != nullptr ? pVersion->_pNext.load( std::memory_order_acquire ) : nullptr; pNext != nullptr;
                  pNext                                   = pVersion->_pNext.load( std::memory_order_acquire ) )
            {
                pLatest  = &pVersion->_pNextValue;
                pVersion = pNext;
            }
            return *pLatest;
        }
        /** @brief `shared_ptr<const T>` 를 받는 함수에 넘길 때입니다(최신 판의 스냅숏). */
        operator shared_ptr<const T>() const { return getShared(); }

        /** @brief 놓습니다. */
        void reset()
        {
            _pHeld.reset();
            _pVersion = nullptr;
        }

        /**
         * @brief 쥔 판을 최신 판으로 옮겨 옛 판들을 놓습니다(쓰기 — 게임 스레드, 틱 밖이나 이 참조를 혼자 쓰는 자리에서).
         * @details 사슬은 다시 읽을 때마다 하나씩 늘고 이 참조가 쥔 동안 옛 판이 남는다. 다시 읽기는 개발 핫 리로드 · 언어 · 품질 변경뿐이라 짧다.
         *          shortcut: 부르는 쪽이 없으면 옛 판이 참조 수명만큼 남는다 — 다시 읽기가 배포에서 잦아지면 캐시가 다시 읽은 뒤 참조를 모아 옮긴다.
         */
        void followLatest()
        {
            const WeakInternVersion<const T>* pLatestVersion = _pVersion;
            while ( pLatestVersion != nullptr && pLatestVersion->_pNext.load( std::memory_order_acquire ) != nullptr )
            {
                pLatestVersion = pLatestVersion->_pNext.load( std::memory_order_acquire );
            }
            *this = SharedAssetRef{ getShared(), pLatestVersion };
        }

        friend bool operator==( const SharedAssetRef& lhs, const SharedAssetRef& rhs ) { return lhs.get() == rhs.get(); }
        friend bool operator!=( const SharedAssetRef& lhs, const SharedAssetRef& rhs ) { return lhs.get() != rhs.get(); }
        friend bool operator==( const SharedAssetRef& lhs, std::nullptr_t ) { return lhs._pHeld == nullptr; }
        friend bool operator!=( const SharedAssetRef& lhs, std::nullptr_t ) { return lhs._pHeld != nullptr; }
        friend bool operator==( std::nullptr_t, const SharedAssetRef& rhs ) { return rhs._pHeld == nullptr; }
        friend bool operator!=( std::nullptr_t, const SharedAssetRef& rhs ) { return rhs._pHeld != nullptr; }

    private:
        shared_ptr<const T>               _pHeld;               ///< 처음 받은 판(사슬의 머리 — 뒤 판들을 살려 둔다)
        const WeakInternVersion<const T>* _pVersion{ nullptr }; ///< `_pHeld` 의 판. 표에서 오지 않았으면 nullptr
    };
} // namespace sw
