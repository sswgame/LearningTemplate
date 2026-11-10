#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityCatalogEditModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 이펙트 셋 · 어빌리티 둘 · 세트 하나 · 큐 연출 하나. 세트는 없는 어빌리티 GA_Missing 을 가리킨다. */
    constexpr const utf8* kCatalogXML = "<AbilityCatalog>\n"
                                        "  <!-- 피해 -->\n"
                                        "  <AbilityEffect id=\"GE_Damage\" duration=\"Instant\">\n"
                                        "    <Cue tag=\"AbilityPresentation.Hit.Fire\"/>\n"
                                        "  </AbilityEffect>\n"
                                        "  <AbilityEffect id=\"GE_Cost\" duration=\"Instant\"/>\n"
                                        "  <AbilityEffect id=\"GE_Cooldown\" duration=\"HasDuration\" seconds=\"1\"/>\n"
                                        "  <Ability id=\"GA_Bolt\" class=\"Projectile\" cost=\"GE_Cost\" cooldown=\"GE_Cooldown\">\n"
                                        "    <Param name=\"damageEffect\" text=\"GE_Damage\"/>\n"
                                        "    <Param name=\"projectile\" text=\"arena_bolt\"/>\n"
                                        "  </Ability>\n"
                                        "  <Ability id=\"GA_Free\" class=\"Projectile\" cost=\"\"/>\n"
                                        "  <AbilitySet id=\"Player\">\n"
                                        "    <Ability id=\"GA_Bolt\" input=\"1\"/>\n"
                                        "    <Ability id=\"GA_Missing\" input=\"2\"/>\n"
                                        "  </AbilitySet>\n"
                                        "  <AbilityPresentation tag=\"AbilityPresentation.Hit\" sound=\"Hit\"/>\n"
                                        "</AbilityCatalog>\n";

    struct AbilityCatalogEditModelTestUtil
    {
        static optional<int32> findReference( const AbilityCatalogEditModel& model, string_view label, string_view targetID )
        {
            const vector<AbilityCatalogReference>& listReference = model.getReferences();
            for ( int32 index = 0; index < static_cast<int32>( listReference.size() ); ++index )
            {
                const AbilityCatalogReference& reference = listReference[static_cast<size_t>( index )];
                if ( string_view( reference._label ) == label && string_view( reference._targetID ) == targetID )
                    return index;
            }
            return nullopt;
        }
    };
    using Util = AbilityCatalogEditModelTestUtil;
} // namespace

// 항목과 참조는 로더의 뜻을 따른다 — 비용 · 쿨다운 · 세트 구성은 필수, 이펙트 이름의 Param 은 연결만, 큐는 가장 가까운 조상 태그의 연출, 빈 cost 는 참조가 아니다.
SW_TEST_CASE( AbilityCatalogEditModelTest, ReferencesFollowTheLoader )
{
    AbilityCatalogEditModel model;
    SW_REQUIRE_TRUE( model.load( kCatalogXML, nullptr ) );
    SW_REQUIRE_EQUAL( 7u, static_cast<uint32>( model.getEntries().size() ) );
    const int32 bolt = model.findEntry( AbilityEntryKind::Ability, "GA_Bolt" ).value();

    vector<int32> listFrom;
    model.collectReferencesFrom( bolt, listFrom );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listFrom.size() ) ); // cost · cooldown · param damageEffect(arena_bolt 는 이펙트가 아니다)
    const optional<int32> param = Util::findReference( model, "param damageEffect", "GE_Damage" );
    SW_REQUIRE_TRUE( param.has_value() );
    SW_EXPECT_TRUE( model.getReferences()[static_cast<size_t>( *param )]._bRequired == SW_FALSE );

    // GA_Free 의 cost="" 는 "없음" 을 적은 것이다.
    model.collectReferencesFrom( model.findEntry( AbilityEntryKind::Ability, "GA_Free" ).value(), listFrom );
    SW_EXPECT_TRUE( listFrom.empty() );

    const optional<int32> cue = Util::findReference( model, "cue", "AbilityPresentation.Hit.Fire" );
    SW_REQUIRE_TRUE( cue.has_value() );
    SW_EXPECT_TRUE( model.getReferences()[static_cast<size_t>( *cue )]._target == model.findEntry( AbilityEntryKind::Presentation, "AbilityPresentation.Hit" ) );

    // 쓰이는 곳: GE_Cost 는 GA_Bolt 하나가 쓴다.
    vector<int32> listTo;
    model.collectReferencesTo( model.findEntry( AbilityEntryKind::Effect, "GE_Cost" ).value(), listTo );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( listTo.size() ) );
    SW_EXPECT_EQUAL( bolt, model.getReferences()[static_cast<size_t>( listTo[0] )]._from );
}

// 풀리지 않은 필수 참조는 그 항목의 문제로 보이고, 참조를 바꾸면 사라진다. 원문의 주석은 남는다.
SW_TEST_CASE( AbilityCatalogEditModelTest, UnresolvedReferenceIsAnIssueUntilFixed )
{
    AbilityCatalogEditModel model;
    SW_REQUIRE_TRUE( model.load( kCatalogXML, nullptr ) );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( model.getIssues().size() ) );
    SW_EXPECT_TRUE( model.findEntry( AbilityEntryKind::AbilitySet, "Player" ) == model.getIssues()[0]._entry );

    const optional<int32> missing = Util::findReference( model, "set ability", "GA_Missing" );
    SW_REQUIRE_TRUE( missing.has_value() );
    SW_REQUIRE_TRUE( model.setReference( *missing, "GA_Free" ) );
    SW_EXPECT_TRUE( model.getIssues().empty() );
    SW_EXPECT_TRUE( model.getDocument().getText().find( "<!-- 피해 -->" ) != string::npos );
    SW_EXPECT_TRUE( model.getDocument().getText().find( "id=\"GA_Free\" input=\"2\"" ) != string::npos );
    // 되돌리기 한 번이면 원래 글이다.
    model.editDocument().undo();
    model.rebuild();
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( model.getIssues().size() ) );
}

// 다른 항목이 쓰는 항목은 지우지 않는다(지우면 그쪽이 로드에서 풀리지 않는다). 새 항목은 겹치지 않는 id 이고, 세트에 어빌리티를 더할 수 있다.
SW_TEST_CASE( AbilityCatalogEditModelTest, UsedEntriesStayAndNewEntriesAreUnique )
{
    AbilityCatalogEditModel model;
    SW_REQUIRE_TRUE( model.load( kCatalogXML, nullptr ) );
    SW_EXPECT_FALSE( model.removeEntry( model.findEntry( AbilityEntryKind::Effect, "GE_Cost" ).value() ) );
    SW_EXPECT_FALSE( model.getDocument().getError().empty() );

    const int32 added = model.addEntry( AbilityEntryKind::Effect ).value();
    SW_EXPECT_TRUE( model.getEntries()[static_cast<size_t>( added )]._kind == AbilityEntryKind::Effect );
    SW_EXPECT_FALSE( model.findEntry( AbilityEntryKind::Effect, "" ).has_value() );
    SW_EXPECT_TRUE( model.removeEntry( added ) );

    const int32 player = model.findEntry( AbilityEntryKind::AbilitySet, "Player" ).value();
    SW_REQUIRE_TRUE( model.addSetMember( player, AbilityEntryKind::Ability, "GA_Free" ) );
    vector<int32> listFrom;
    model.collectReferencesFrom( model.findEntry( AbilityEntryKind::AbilitySet, "Player" ).value(), listFrom );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listFrom.size() ) );
}
