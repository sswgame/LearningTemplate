/**
 * @file PlaygroundAbilitiesExperience.cpp
 * @brief 허브 Experience(`experiences/hub.experience.xml`)의 키트 `PlaygroundAbilities` 입니다 — 지역 효과 시연(`maps/zoneshowcase` · `maps/surfaceinteraction`)과 허브의
 *        이펙트 · 어빌리티 · 세트(`data/abilities.xml`)와 기본 어빌리티 클래스를 Experience 범위에 겁니다.
 * @details 게임 인스턴스의 `onInitialize` 가 멤버로 쥐고 걸던 것을 허브 Experience 의 `catalogs` 로 옮겼다(허브 이전 3 단계). 허브는 인스턴스 내내 켜진 바탕이라 허브 밖 첫 씬도 찾는다.
 *          게임 모듈이 등록하는 까닭: 기본 클래스 묶음은 이 게임의 것이다 — 언리얼 Lyra 의 게임 전용 Game Feature(ShooterCore)와 같은 자리. 둘째 게임이 같은 묶음을 쓰면 GameFramework 로 옮긴다.
 */
#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceKitRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

namespace sw
{
    namespace
    {
        /** @brief 어빌리티 클래스(기본 — Generic · Combat 세트, ApplyEffects)와 카탈로그(종류 `abilities`)를 겁니다. 지역 프로필이 읽을 때 참조를 이 카탈로그로 본다. */
        [[nodiscard]] bool activatePlaygroundAbilities( ExperienceScope& scope )
        {
            return scope.emplaceService<AbilityClassRegistry>() && scope.loadCatalog<AbilityCatalog>();
        }
    } // namespace

    SW_EXPERIENCE_KIT( PlaygroundAbilities, "PlaygroundAbilities", &activatePlaygroundAbilities );
} // namespace sw
