/**
 * @file BoundAssetRef.h
 * @brief 게임 모드가 판을 열며 받은 에셋을 쥐고, 규칙 객체가 묶인 판만 읽는 참조 형식입니다.
 * @details 규칙 객체는 카탈로그 행의 번호 · 날 포인터를 든다. 최신 판을 읽는 `SharedAssetRef` 를 게임 모드가 쥐면 다시 읽힌 뒤 규칙 객체를 다시 묶기 전(묶지 못하면 끝까지)
 *          뷰 · 명령이 새 판에서 옛 행 번호를 찾는다. 그래서 게임 모드의 멤버는 이 형식이고, 가리키는 판은 `GameModeComponent` 가 규칙 객체를 다시 묶는 자리에서만 바꾼다 —
 *          읽는 쪽은 누구나(뷰 · 입력 · 디렉터) 규칙 객체와 같은 판을 본다. 언리얼 DataTable 행 핸들이 표 한 벌에서만 행을 찾는 자리이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

#include <cstddef>

namespace sw
{
    template <typename T>
    class BoundAssetRef;

    /**
     * @class BoundAsset
     * @brief 게임 모드가 따라가는 에셋 하나입니다 — 규칙 객체가 묶인 판과, 다음 판을 열 때까지 살려 둘 판들을 쥡니다. `GameModeComponent` 만 바꿉니다.
     */
    class BoundAsset
    {
        friend class GameModeComponent;
        template <typename T>
        friend class BoundAssetRef;

    private:
        Delegate<shared_ptr<const void>()> _takeLatest{}; ///< 지금 최신 판
        vector<shared_ptr<const void>>     _listHeld{};   ///< 연 판부터 최신 판까지(뒤가 가장 나중에 본 판) — 규칙 객체가 날 포인터로 빌린다
        shared_ptr<const void>             _pBound{};     ///< 규칙 객체가 묶인 판
    };

    /**
     * @class BoundAssetRef
     * @brief 규칙 객체가 묶인 @p T 한 판을 읽습니다. `GameModeComponent::acquireRequired` · `acquireBoundCatalog` 만 채웁니다.
     * @details 가리키는 판은 틱 밖(게임 스레드)에서만 바뀌므로 틱 안 · 워커 어디서 읽어도 된다. 사본도 같은 판을 따라간다.
     */
    template <typename T>
    class BoundAssetRef
    {
        friend class GameModeComponent;

    public:
        BoundAssetRef() = default;

        /** @brief 규칙 객체가 묶인 판입니다. 비었으면 nullptr 입니다. */
        const T* get() const { return _pAsset != nullptr ? static_cast<const T*>( _pAsset->_pBound.get() ) : nullptr; }
        const T* operator->() const { return get(); }
        const T& operator*() const { return *get(); }
        explicit operator bool() const { return get() != nullptr; }
        /** @brief 규칙 객체가 묶인 판을 소유 포인터로 돌려줍니다(어느 판으로 지었는지 기억하는 쪽 — 쥔 동안 주소가 다시 쓰이지 않는다). */
        shared_ptr<const T> snapshot() const { return _pAsset != nullptr ? shared_ptr<const T>( _pAsset->_pBound, get() ) : nullptr; }
        /** @brief 놓습니다. */
        void reset() { _pAsset.reset(); }

        friend bool operator==( const BoundAssetRef& lhs, std::nullptr_t ) { return lhs.get() == nullptr; }
        friend bool operator!=( const BoundAssetRef& lhs, std::nullptr_t ) { return lhs.get() != nullptr; }
        friend bool operator==( std::nullptr_t, const BoundAssetRef& rhs ) { return rhs.get() == nullptr; }
        friend bool operator!=( std::nullptr_t, const BoundAssetRef& rhs ) { return rhs.get() != nullptr; }

    private:
        explicit BoundAssetRef( shared_ptr<const BoundAsset> pAsset )
            : _pAsset{ std::move( pAsset ) }
        {
        }

    private:
        shared_ptr<const BoundAsset> _pAsset{};
    };
} // namespace sw
