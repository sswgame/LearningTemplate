/**
 * @file AssetReloadWatch.h
 * @brief 공유 캐시의 다시 읽기 횟수를 지켜보는 값 형식입니다. "마지막으로 본 횟수" 칸과 대조 · 갱신 줄을 손으로 적지 않게 합니다.
 * @details 에셋 종류가 칸의 형식에 들어 있어 다른 종류의 횟수와 대조할 수 없고, 대조와 갱신이 한 곳이라 갱신을 빠뜨려 매 틱 다시 짓는 일이 없습니다.
 *          캐시의 횟수(`SharedAssetCache::getReloadCount`)는 비공개이고 이 형식만 읽는다(friend) — 손 대조 칸(`_seen…ReloadCount`)을 형식으로 막는다.
 *          여러 캐시의 판을 굽기 키에 넣는 자리도 감시를 종류마다 두고 본 값(`getSeenCount`)을 섞는다. 저장 구분(`SaveReloadGuard`)은 이 형식의 일이 아닙니다.
 *          언리얼 `FCoreUObjectDelegates::OnObjectPropertyChanged` · Godot `Resource::changed` 는 구독이고 여기는 틱에서 묻는 쪽이다 — 쥔 객체를 제자리에서 바꾸는 캐시라 틱 밖 콜백을 두지 않는다(기억에 의존).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"

namespace sw
{
    /**
     * @class AssetReloadWatch
     * @brief 에셋 종류 @p TAsset 의 공유 캐시 다시 읽기 횟수를 기억합니다. 처음 값은 0 이라 아직 받지 않은 쪽은 다시 읽은 적이 있으면 낡음으로 봅니다.
     */
    template <typename TAsset>
    class AssetReloadWatch
    {
    public:
        /** @brief 지금의 횟수를 봤다고 적습니다(에셋을 받을 때 — 받기 전에 적어야 받는 도중의 다시 읽기를 놓치지 않는다). */
        void markSeen() { _seenCount = SharedAssetCache<TAsset>::getReloadCount(); }
        /** @brief 마지막으로 본 뒤에 다시 읽혔으면 true 입니다. 적지는 않습니다(다시 받는 함수가 `markSeen` 을 부른다). */
        [[nodiscard]] bool isStale() const { return _seenCount != SharedAssetCache<TAsset>::getReloadCount(); }
        /** @brief 마지막으로 본 뒤에 다시 읽혔으면 지금 횟수를 적고 true 입니다(true 는 다시 읽힐 때마다 한 번). */
        [[nodiscard]] bool takeChanged()
        {
            const uint32 count = SharedAssetCache<TAsset>::getReloadCount();
            if ( count == _seenCount )
                return false;
            _seenCount = count;
            return true;
        }
        /** @brief 마지막으로 본 횟수입니다 — 지은 결과의 키(굽기 키)에 판으로 섞는다. 낡음 판정은 `isStale` · `takeChanged` 가 한다. */
        [[nodiscard]] uint32 getSeenCount() const { return _seenCount; }

    private:
        uint32 _seenCount{ 0 };
    };
} // namespace sw
