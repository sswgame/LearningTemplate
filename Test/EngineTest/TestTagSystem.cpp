#include "pch.h"

#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) TagSystemTest — 계층 리터럴·매칭
// ------------------------------------------------------------------------------
/**
 * @brief [TagSystemTest] 계층 리터럴의 부모 해시
 */
SW_TEST_CASE( TagSystemTest, ParentHashOnHierarchicalLiteral )
{
    constexpr TagID root    = "Faction"_tag;
    constexpr TagID child   = "Faction.Player"_tag;
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

/**
 * @brief [TagSystemTest] 문자열에서 만든 ID 로도 리터럴 태그를 찾는다
 * @details 태그 ID 를 구하는 코드가 **세 곳**에 각자 있었고 규칙이 갈려 있었다 — `""_tag` 와
 *          `TagID::request` 는 대소문자를 구별했고, 에디터 Hierarchy 의 `tag:` 필터는
 *          `computeHash64` 를 기본 인자로 불러 무시했다. 저장소의 태그는 전부 대문자로 시작하므로
 *          (`Collider` · `Sprite` · `UI` · `Faction.Player` …) **그 필터는 하나도 찾지 못했다.**
 *          이제 셋 다 `TagID::computeId` 를 쓴다.
 */
SW_TEST_CASE( TagSystemTest, IdBuiltFromStringFindsLiteralTag )
{
    TagContainer owned{ "Collider"_tag, "Faction.Player"_tag };

    // 필터가 하는 일 그대로 — 문자열만 들고 ID 를 만들어 묻는다.
    const string_view exact{ "Collider" };
    SW_EXPECT_TRUE( owned.hasTag( TagID{ TagID::computeId( exact.data(), exact.size() ), exact.data() } ) );

    // 대소문자는 무시한다. intern 이 이미 무시하므로 ID 도 같아야 앞뒤가 맞는다.
    const string_view lowered{ "collider" };
    SW_EXPECT_TRUE( owned.hasTag( TagID{ TagID::computeId( lowered.data(), lowered.size() ), lowered.data() } ) );

    // 계층도 잡힌다 — 문자열을 함께 넘기므로 isSubtagOf 가 돈다.
    const string_view parent{ "Faction" };
    SW_EXPECT_TRUE( owned.hasTag( TagID{ TagID::computeId( parent.data(), parent.size() ), parent.data() } ) );

    // 없는 태그를 찾아내면 안 된다.
    const string_view absent{ "Sprite" };
    SW_EXPECT_FALSE( owned.hasTag( TagID{ TagID::computeId( absent.data(), absent.size() ), absent.data() } ) );
}

/**
 * @brief [TagSystemTest] 리터럴과 런타임 요청이 같은 태그를 만든다
 * @details `TagID::request` 는 문자열을 `hashed_string` 으로 intern 하는데 그 intern 이 대소문자를
 *          무시한다. ID 만 구별하면 **같은 문자열을 가리키는 두 태그가 서로 다른 ID** 를 갖는다.
 */
SW_TEST_CASE( TagSystemTest, LiteralAndRuntimeRequestAgree )
{
    constexpr TagID literal = "Collider"_tag;

    SW_EXPECT_TRUE( TagID::request( "Collider" ) == literal );
    SW_EXPECT_TRUE( TagID::request( "collider" ) == literal );
    SW_EXPECT_TRUE( TagID::request( "COLLIDER" ) == literal );

    // 다른 태그까지 같아지면 안 된다.
    SW_EXPECT_TRUE( TagID::request( "Collider2" ) != literal );

    // 계층 비교도 같은 규칙이어야 한다.
    SW_EXPECT_TRUE( TagID::request( "faction.player" ).isSubtagOf( "Faction"_tag ) );
    SW_EXPECT_FALSE( TagID::request( "factionary" ).isSubtagOf( "Faction"_tag ) );
}

/**
 * @brief [TagSystemTest] 정확 매칭 vs 포함 매칭
 */
SW_TEST_CASE( TagSystemTest, ExactVsSubsumptionMatch )
{
    constexpr TagID combat    = "State.Combat"_tag;
    constexpr TagID attacking = "State.Combat.Attacking"_tag;

    TagContainer container{ attacking };

    SW_EXPECT_TRUE( container.hasTag( attacking, true ) );
    SW_EXPECT_FALSE( container.hasTag( combat, true ) );
    SW_EXPECT_TRUE( container.hasTag( combat, false ) );
    SW_EXPECT_TRUE( container.hasTag( attacking, false ) );
}

/**
 * @brief [TagSystemTest] hasAll / hasAny / match
 */
SW_TEST_CASE( TagSystemTest, HasAllHasAnyAndMatch )
{
    TagContainer owned{ "Status.Buff"_tag, "Status.Invincible"_tag, "Team.Ally"_tag };
    TagContainer needAll{ "Status.Buff"_tag, "Team.Ally"_tag };
    TagContainer needAny{ "Status.Poison"_tag, "Team.Ally"_tag };
    TagContainer forbidden{ "Status.Dead"_tag };

    SW_EXPECT_TRUE( owned.hasAllTags( needAll ) );
    SW_EXPECT_TRUE( owned.hasAnyTag( needAny ) );
    SW_EXPECT_TRUE( owned.matchesTags( needAll, forbidden ) );

    TagContainer missing{ "Status.Buff"_tag, "Status.Haste"_tag };
    SW_EXPECT_FALSE( owned.hasAllTags( missing ) );

    TagContainer blocked{ "Status.Invincible"_tag };
    SW_EXPECT_FALSE( owned.matchesTags( needAll, blocked ) );
}

/**
 * @brief [TagSystemTest] 추가·삭제·중복 제거·클리어
 */
SW_TEST_CASE( TagSystemTest, AddRemoveDedupAndClear )
{
    TagContainer tags;
    const TagID  a = "A"_tag;
    const TagID  b = "B"_tag;

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

/**
 * @brief [TagSystemTest] GameObject addTag가 TagComponent를 만들고 질의한다
 */
SW_TEST_CASE( TagSystemTest, GameObjectTagContainerQuery )
{
    sw::GameObjectManager manager;
    sw::GameObject*       obj = manager.createGameObject( sw::hashed_string( "PlayerActor" ) );
    SW_ASSERT_NOT_NULL( obj );

    obj->addTag( "Character.Player"_tag );
    obj->addTag( "Buff.Invincible"_tag );

    SW_EXPECT_TRUE( obj->hasTag( "Character.Player"_tag ) );
    SW_EXPECT_TRUE( obj->hasTag( "Character"_tag, false ) ); // 서브태그 포함 매칭
    SW_EXPECT_FALSE( obj->hasTag( "Character"_tag, true ) ); // 정확 매칭
    SW_EXPECT_TRUE( obj->hasTag( "Buff.Invincible"_tag ) );

    obj->removeTag( "Buff.Invincible"_tag );
    SW_EXPECT_FALSE( obj->hasTag( "Buff.Invincible"_tag ) );
    SW_EXPECT_TRUE( obj->hasTag( "Character.Player"_tag ) );

    obj->clearTags();
    SW_EXPECT_FALSE( obj->hasTag( "Character.Player"_tag ) );
}

/**
 * @brief [TagSystemTest] TagQuery 기본 매칭 (All/Any/None) 및 서브태그 계층 검증
 */
SW_TEST_CASE( TagSystemTest, TagQueryBasicAndHierarchical )
{
    TagContainer playerTags{ "Faction.Player.Hero"_tag, "Status.Buff.Speed"_tag };

    // 1) AllMatch
    TagQuery allQuery = TagQuery::createAllMatch( TagContainer{ "Faction.Player"_tag, "Status.Buff"_tag } );
    SW_EXPECT_TRUE( allQuery.matches( playerTags ) );

    TagQuery allFailQuery = TagQuery::createAllMatch( TagContainer{ "Faction.Player"_tag, "Status.Debuff"_tag } );
    SW_EXPECT_FALSE( allFailQuery.matches( playerTags ) );

    // 2) AnyMatch
    TagQuery anyQuery = TagQuery::createAnyMatch( TagContainer{ "Faction.Enemy"_tag, "Status.Buff.Speed"_tag } );
    SW_EXPECT_TRUE( anyQuery.matches( playerTags ) );

    TagQuery anyFailQuery = TagQuery::createAnyMatch( TagContainer{ "Faction.Enemy"_tag, "Status.Poison"_tag } );
    SW_EXPECT_FALSE( anyFailQuery.matches( playerTags ) );

    // 3) NoMatch
    TagQuery noQuery = TagQuery::createNoMatch( TagContainer{ "Faction.Enemy"_tag, "Status.Dead"_tag } );
    SW_EXPECT_TRUE( noQuery.matches( playerTags ) );

    TagQuery noFailQuery = TagQuery::createNoMatch( TagContainer{ "Status.Buff"_tag } );
    SW_EXPECT_FALSE( noFailQuery.matches( playerTags ) );
}

/**
 * @brief [TagSystemTest] TagQueryExpr AST 복합 불리언 표현식 트리 (AND/OR/NOT 조합) 검증
 */
SW_TEST_CASE( TagSystemTest, TagQueryComplexAstExpression )
{
    // Query: (Faction.Player OR Faction.Ally) AND NOT (Status.Dead OR Status.Stunned)
    TagQueryExpr exprFactionAny;
    exprFactionAny._type = TagQueryExprType::AnyTagsMatch;
    exprFactionAny._tags.addTag( "Faction.Player"_tag );
    exprFactionAny._tags.addTag( "Faction.Ally"_tag );

    TagQueryExpr exprDebuffAny;
    exprDebuffAny._type = TagQueryExprType::AnyTagsMatch;
    exprDebuffAny._tags.addTag( "Status.Dead"_tag );
    exprDebuffAny._tags.addTag( "Status.Stunned"_tag );

    TagQueryExpr exprNotDebuff;
    exprNotDebuff._type = TagQueryExprType::NotExprMatch;
    exprNotDebuff._listSubExpr.push_back( std::move( exprDebuffAny ) );

    TagQueryExpr rootExpr;
    rootExpr._type = TagQueryExprType::AllExprMatch;
    rootExpr._listSubExpr.push_back( std::move( exprFactionAny ) );
    rootExpr._listSubExpr.push_back( std::move( exprNotDebuff ) );

    TagQuery compositeQuery = TagQuery::createExpression( std::move( rootExpr ) );

    // Case 1: Active Player -> Match
    TagContainer livePlayer{ "Faction.Player.Warrior"_tag, "Status.Buff.Shield"_tag };
    SW_EXPECT_TRUE( compositeQuery.matches( livePlayer ) );

    // Case 2: Dead Player -> Fail (NOT debuff violated)
    TagContainer deadPlayer{ "Faction.Player.Warrior"_tag, "Status.Dead"_tag };
    SW_EXPECT_FALSE( compositeQuery.matches( deadPlayer ) );

    // Case 3: Live Enemy -> Fail (Faction mismatch)
    TagContainer liveEnemy{ "Faction.Enemy.Orc"_tag, "Status.Buff.Berserk"_tag };
    SW_EXPECT_FALSE( compositeQuery.matches( liveEnemy ) );

    // Case 4: Ally -> Match
    TagContainer liveAlly{ "Faction.Ally.Healer"_tag };
    SW_EXPECT_TRUE( compositeQuery.matches( liveAlly ) );
}

/**
 * @brief [TagSystemTest] GameObject 및 TagComponent의 TagQuery 연동 질의 검증
 */
SW_TEST_CASE( TagSystemTest, GameObjectAndTagComponentQueryIntegration )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObj = manager.createGameObject( sw::hashed_string( "HeroObject" ) );
    SW_ASSERT_NOT_NULL( pObj );

    pObj->addTag( "Faction.Player"_tag );
    pObj->addTag( "State.Buff.Haste"_tag );

    TagQuery goodQuery = TagQuery::createAllMatch( TagContainer{ "Faction.Player"_tag, "State.Buff"_tag } );
    TagQuery badQuery  = TagQuery::createAllMatch( TagContainer{ "Faction.Player"_tag, "State.Debuff"_tag } );

    SW_EXPECT_TRUE( pObj->matchesTagQuery( goodQuery ) );
    SW_EXPECT_FALSE( pObj->matchesTagQuery( badQuery ) );

    sw::TagComponent* pTagComp = pObj->getComponent<sw::TagComponent>();
    SW_ASSERT_NOT_NULL( pTagComp );

    SW_EXPECT_TRUE( pTagComp->matchesQuery( goodQuery ) );
    SW_EXPECT_FALSE( pTagComp->matchesQuery( badQuery ) );
}

/**
 * @brief [TagSystemTest] 다단계(4단계) 계층 매칭 및 태그 배치 추가/삭제 검증
 */
SW_TEST_CASE( TagSystemTest, DeepHierarchyAndBatchOperations )
{
    constexpr TagID root     = "Game"_tag;
    constexpr TagID level1   = "Game.Unit"_tag;
    constexpr TagID level2   = "Game.Unit.Hero"_tag;
    constexpr TagID level3   = "Game.Unit.Hero.Mage"_tag;
    constexpr TagID sibling3 = "Game.Unit.Hero.Warrior"_tag;

    SW_EXPECT_TRUE( level3.isSubtagOf( level2 ) );
    SW_EXPECT_TRUE( level3.isSubtagOf( level1 ) );
    SW_EXPECT_TRUE( level3.isSubtagOf( root ) );
    SW_EXPECT_FALSE( level3.isSubtagOf( sibling3 ) );

    TagContainer container;
    container.addTag( level3 );
    SW_EXPECT_TRUE( container.hasTag( level3, true ) );
    SW_EXPECT_TRUE( container.hasTag( level2, false ) );
    SW_EXPECT_TRUE( container.hasTag( level1, false ) );
    SW_EXPECT_TRUE( container.hasTag( root, false ) );
    SW_EXPECT_FALSE( container.hasTag( sibling3, false ) );

    // 런타임 TagID::request 및 동적 태그 검증
    TagID dynamicTag = TagID::request( "Runtime.Dynamic.Effect" );
    SW_EXPECT_TRUE( dynamicTag.isValid() );
    SW_EXPECT_TRUE( dynamicTag.isSubtagOf( TagID::request( "Runtime.Dynamic" ) ) );
    SW_EXPECT_TRUE( dynamicTag.isSubtagOf( TagID::request( "Runtime" ) ) );

    container.addTag( dynamicTag );
    SW_EXPECT_EQUAL( 2u, container.getTagCount() );
    SW_EXPECT_TRUE( container.hasTag( dynamicTag, true ) );

    TagContainer matchReq{ level3, dynamicTag };
    SW_EXPECT_TRUE( container.hasAllTags( matchReq ) );

    container.removeTag( dynamicTag );
    SW_EXPECT_EQUAL( 1u, container.getTagCount() );
    SW_EXPECT_FALSE( container.hasTag( dynamicTag, true ) );
}

/**
 * @brief [TagSystemTest] 숫자 ID로만 복원된 역직렬화 TagID의 문자열 레지스트리 복원 및 isSubtagOf 매칭 검증
 */
SW_TEST_CASE( TagSystemTest, DeserializedTagIDStringRecoveryAndHierarchyMatching )
{
    // 1) 런타임 등록으로 원본 태그 생성
    TagID  originalTag = TagID::request( "Skill.Spell.Fireball" );
    TagID  rootTag     = TagID::request( "Skill" );
    uint64 rawId       = originalTag._id;

    // 2) 역직렬화 모의: 숫자 ID만 가진 TagID 복원
    TagID restoredTag( rawId );
    SW_EXPECT_TRUE( restoredTag.isValid() );
    SW_EXPECT_EQUAL( originalTag._id, restoredTag._id );
    SW_EXPECT_TRUE( originalTag == restoredTag );

    // 3) _pString이 nullptr인 상태에서도 전역 레지스트리를 통해 isSubtagOf가 정확히 동작하는지 검증
    SW_EXPECT_TRUE( restoredTag.isSubtagOf( rootTag ) );
    SW_EXPECT_TRUE( restoredTag.isSubtagOf( TagID::request( "Skill.Spell" ) ) );
    SW_EXPECT_FALSE( restoredTag.isSubtagOf( TagID::request( "Skill.Melee" ) ) );
}

// ------------------------------------------------------------------------------
// 2) TagSystemTest — 계층 리터럴·매칭
// ------------------------------------------------------------------------------
/**
 * @brief [TagSystemTest] 정수 리터럴과 계층 포함
 */
SW_TEST_CASE( TagSystemTest, IntegerLiteralAndHierarchicalSubsumption )
{
    sw::GameObjectManager manager;
    constexpr TagID       tagAttacking = "State.Combat.Attacking"_tag;
    constexpr TagID       tagCombat    = "State.Combat"_tag;
    constexpr TagID       tagState     = "State"_tag;

    SW_EXPECT_TRUE( tagAttacking.isValid() );
    SW_EXPECT_TRUE( tagCombat.isValid() );

    SW_EXPECT_TRUE( tagAttacking.isSubtagOf( tagCombat ) );
    SW_EXPECT_TRUE( tagAttacking.isSubtagOf( tagState ) ); // 전체 부모 체인
    SW_EXPECT_TRUE( tagCombat.isSubtagOf( tagState ) );
    SW_EXPECT_FALSE( tagState.isSubtagOf( tagAttacking ) );

    TagContainer container{ tagAttacking };
    SW_EXPECT_TRUE( container.hasTag( tagAttacking, true ) );
    SW_EXPECT_TRUE( container.hasTag( tagState, false ) );

    TagContainer required{ tagAttacking };
    TagContainer forbidden{ "State.Dead"_tag };
    SW_EXPECT_TRUE( container.matchesTags( required, forbidden ) );
}
