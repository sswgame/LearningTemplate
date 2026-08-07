/**
 * @file TestTagSystem.cpp
 * @brief TagID hierarchy and TagContainer matching
 */
#include "TestFramework.h"
#include "Core/Object/TagSystem.h"

using namespace sw;

SW_TEST_CASE( TagSystemTest, ParentHashOnHierarchicalLiteral )
{
	constexpr TagID root	= "Faction"_tag;
	constexpr TagID child	= "Faction.Player"_tag;
	constexpr TagID sibling = "Faction.Enemy"_tag;

	SW_ASSERT_TRUE( root.isValid() );
	SW_ASSERT_TRUE( child.isValid() );
	SW_EXPECT_TRUE( child.isSubtagOf( root ) );
	SW_EXPECT_FALSE( root.isSubtagOf( child ) );
	SW_EXPECT_FALSE( child.isSubtagOf( sibling ) );
	SW_EXPECT_TRUE( child != sibling );

	constexpr TagID deep = "Faction.Player.Scout"_tag;
	SW_EXPECT_TRUE( deep.isSubtagOf( child ) );
	SW_EXPECT_TRUE( deep.isSubtagOf( root ) );
}

SW_TEST_CASE( TagSystemTest, ExactVsSubsumptionMatch )
{
	constexpr TagID combat	  = "State.Combat"_tag;
	constexpr TagID attacking = "State.Combat.Attacking"_tag;

	TagContainer container{ attacking };

	SW_EXPECT_TRUE( container.hasTag( attacking, true ) );
	SW_EXPECT_FALSE( container.hasTag( combat, true ) );
	SW_EXPECT_TRUE( container.hasTag( combat, false ) );
	SW_EXPECT_TRUE( container.hasTag( attacking, false ) );
}

SW_TEST_CASE( TagSystemTest, HasAllHasAnyAndMatch )
{
	TagContainer owned{ "Status.Buff"_tag, "Status.Invincible"_tag, "Team.Ally"_tag };
	TagContainer needAll{ "Status.Buff"_tag, "Team.Ally"_tag };
	TagContainer needAny{ "Status.Poison"_tag, "Team.Ally"_tag };
	TagContainer forbidden{ "Status.Dead"_tag };

	SW_EXPECT_TRUE( owned.hasAllTags( needAll ) );
	SW_EXPECT_TRUE( owned.hasAnyTag( needAny ) );
	SW_EXPECT_TRUE( owned.matchTags( needAll, forbidden ) );

	TagContainer missing{ "Status.Buff"_tag, "Status.Haste"_tag };
	SW_EXPECT_FALSE( owned.hasAllTags( missing ) );

	TagContainer blocked{ "Status.Invincible"_tag };
	SW_EXPECT_FALSE( owned.matchTags( needAll, blocked ) );
}

SW_TEST_CASE( TagSystemTest, AddRemoveDedupAndClear )
{
	TagContainer tags;
	const TagID	 a = "A"_tag;
	const TagID	 b = "B"_tag;

	tags.addTag( a );
	tags.addTag( a );
	tags.addTag( b );
	SW_EXPECT_EQUAL( 2u, tags.getTagCount() );

	tags.removeTag( a );
	SW_EXPECT_EQUAL( 1u, tags.getTagCount() );
	SW_EXPECT_FALSE( tags.hasTag( a, true ) );
	SW_EXPECT_TRUE( tags.hasTag( b, true ) );

	tags.clear();
	SW_EXPECT_EQUAL( 0u, tags.getTagCount() );
	SW_EXPECT_EMPTY( tags.getTags() );
}
