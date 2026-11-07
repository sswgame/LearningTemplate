// AbilityCatalogReferencesTest — 어빌리티 카탈로그의 연결 계약: 어빌리티 → 비용 · 쿨다운 이펙트, 이펙트 → 부여 어빌리티, 세트 → 어빌리티 · 이펙트가 없으면 로드 실패
// (참조 표 `engine/settings/datacatalogreferences.xml`), XML 안의 같은 종류 id 겹침은 오류이고 코드 추가 API 의 교체는 그대로다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

using namespace sw;

/** @brief [AbilityCatalogReferencesTest] 모든 연결이 파일 안에 있으면 읽힌다(대상이 뒤에 적혀도 된다) */
SW_TEST_CASE( AbilityCatalogReferencesTest, CleanLinksLoad )
{
    AbilityCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<AbilityCatalog>
  <AbilitySet id="AS_Start"><Ability id="GA_Strike"/><Effect id="GE_Buff"/></AbilitySet>
  <Ability id="GA_Strike" class="ApplyEffects" cooldown="GE_Cooldown"/>
  <AbilityEffect id="GE_Cooldown" duration="HasDuration" seconds="1"/>
  <AbilityEffect id="GE_Buff" duration="Infinite"><GrantAbility id="GA_Strike"/></AbilityEffect>
</AbilityCatalog>)",
                                             "AbilityCatalogReferencesTest" ) );
    SW_EXPECT_NOT_NULL( catalog.findAbilitySet( "AS_Start" ) );
}

/** @brief [AbilityCatalogReferencesTest] 없는 비용 이펙트 · 부여 어빌리티 · 세트의 어빌리티 · 세트의 이펙트는 하나마다 알리고 로드가 실패한다 */
SW_TEST_CASE( AbilityCatalogReferencesTest, BrokenLinksFailTheLoad )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" ); // 넷 — 같은 id 의 선언 하나가 모두 센다
    AbilityCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<AbilityCatalog>
  <Ability id="GA_Strike" class="ApplyEffects" cost="GE_Missing"/>
  <AbilityEffect id="GE_Buff" duration="Infinite"><GrantAbility id="GA_Missing"/></AbilityEffect>
  <AbilitySet id="AS_Start"><Ability id="GA_Nobody"/><Effect id="GE_Nobody"/></AbilitySet>
</AbilityCatalog>)",
                                              "AbilityCatalogReferencesTest" ) );
}

/** @brief [AbilityCatalogReferencesTest] 한 글 안에서 같은 종류 id 를 두 번 적으면 오류이고 로드가 실패한다. 다른 글이 같은 id 를 다시 적는 합치기와 코드 추가 API 의 교체는 그대로다 */
SW_TEST_CASE( AbilityCatalogReferencesTest, DuplicateIDInOneTextIsAnErrorButMergeAndCodeReplace )
{
    SW_EXPECT_LOG( "XMLCatalog.DuplicateID" );
    AbilityCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<AbilityCatalog>
  <AbilityEffect id="GE_Twice" duration="HasDuration" seconds="1"/>
  <AbilityEffect id="GE_Twice" duration="HasDuration" seconds="2"/>
</AbilityCatalog>)",
                                              "AbilityCatalogReferencesTest" ) ); // 실패한 로드의 내용은 쓰지 않는다(공유 캐시는 실패한 파일을 내주지 않는다)

    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<AbilityCatalog><AbilityEffect id="GE_Once" duration="HasDuration" seconds="1"/></AbilityCatalog>)", "first" ) );
    const shared_ptr<const AbilityEffectDef> pFirst = catalog.findEffect( "GE_Once" );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<AbilityCatalog><AbilityEffect id="GE_Once" duration="HasDuration" seconds="2"/></AbilityCatalog>)", "second" ) ); // 뒤 글이 이긴다
    SW_EXPECT_TRUE( catalog.findEffect( "GE_Once" ) != pFirst );

    const shared_ptr<const AbilityEffectDef> pSecond = catalog.findEffect( "GE_Once" );
    (void)catalog.addEffect( *pSecond ); // 코드 추가 API — 같은 id 를 바꾼다(오류가 아니다)
    SW_EXPECT_TRUE( catalog.findEffect( "GE_Once" ) != pSecond );
}
