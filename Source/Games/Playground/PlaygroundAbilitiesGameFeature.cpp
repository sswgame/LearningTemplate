/**
 * @file PlaygroundAbilitiesGameFeature.cpp
 * @brief 게임 기능 `PlaygroundAbilities` 입니다 — gamesettings `gameFeatures` 가 게임 범위에 켜, 지역 효과 시연(`maps/zoneshowcase` · `maps/surfaceinteraction`)과 마을의
 *        이펙트 · 어빌리티 · 세트(`data/abilities.xml`)와 기본 어빌리티 클래스를 겁니다. 미니게임 키트의 클래스(아레나)는 그 키트의 게임 기능이 이 레지스트리에 올리고 내린다.
 *        미니게임이 나눠 쓰는 탄 표(`data/projectiles.xml` — 아레나 · 사격장)도 여기 한 곳에서 건다 — 미니게임마다 걸면 같은 서비스를 둘이 건다.
 * @details 게임 인스턴스의 `onInitialize` 가 멤버로 쥐고 걸던 것을 게임 기능으로 옮겼다. 게임 범위라 인스턴스 내내 켜져 첫 씬부터 찾는다(카탈로그 `abilities` 는
 *          gamesettings 데이터 항목에 경로가 있어 선언된 것이다). 게임 모듈이 등록하는 까닭: 기본 클래스 묶음은 이 게임의 것이다 — 언리얼 Lyra 의 게임 전용 Game Feature(ShooterCore)와
 *          같은 자리. 둘째 게임이 같은 묶음을 쓰면 GameFramework 로 옮긴다.
 */
#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 어빌리티 클래스(기본 — Generic · Combat 세트, ApplyEffects)와 카탈로그(종류 `abilities` · `projectiles`)를 겁니다. 지역 프로필이 읽을 때 참조를 이 카탈로그로 본다. */
        [[nodiscard]] bool activatePlaygroundAbilities( GameFeatureScope& scope )
        {
            return scope.emplaceService<AbilityClassRegistry>() && scope.loadCatalog<AbilityCatalog>() && scope.loadCatalog<ProjectileCatalog>();
        }
    } // namespace

    SW_GAME_FEATURE( PlaygroundAbilities, "PlaygroundAbilities", &activatePlaygroundAbilities );
} // namespace sw
