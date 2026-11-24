#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/KartTrack.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"

namespace sw
{
    namespace
    {
        /** @brief 트랙(`KartTrackCatalog`, 팩의 `data/karttracks.xml`)을 게임 기능 범위에 겁니다 — 미니게임 정의의 catalogs 에 `KartTrackCatalog` 가 있어야 한다. */
        [[nodiscard]] bool activateKartRacing( GameFeatureScope& scope )
        {
            return scope.loadCatalog<KartTrackCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( KartRacing, "KartRacing", &activateKartRacing );
} // namespace sw
