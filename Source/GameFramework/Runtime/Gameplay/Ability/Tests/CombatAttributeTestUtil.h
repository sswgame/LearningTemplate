/**
 * @file CombatAttributeTestUtil.h
 * @brief 전투 어트리뷰트 세트를 테스트 값으로 짓습니다(코드에는 기본 능력치가 없다 — 게임은 어빌리티 카탈로그가 적는다).
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

namespace sw::test
{
    /** @brief 체력 · 마나 100, 공격력 10, 방어 0, 이동 속도 5 인 전투 세트입니다. */
    inline unique_ptr<AttributeSet> makeCombatAttributeSet()
    {
        unique_ptr<CombatAttributeSet> pSet = make_unique<CombatAttributeSet>();
        pSet->defineAttribute( CombatAttributes::health(), 100.0f );
        pSet->defineAttribute( CombatAttributes::maxHealth(), 100.0f );
        pSet->defineAttribute( CombatAttributes::mana(), 100.0f );
        pSet->defineAttribute( CombatAttributes::maxMana(), 100.0f );
        pSet->defineAttribute( CombatAttributes::attackPower(), 10.0f );
        pSet->defineAttribute( CombatAttributes::armor(), 0.0f );
        pSet->defineAttribute( CombatAttributes::moveSpeed(), 5.0f );
        return pSet;
    }
} // namespace sw::test
