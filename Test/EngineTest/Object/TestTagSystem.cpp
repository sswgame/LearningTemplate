#include "pch.h"

#include "Core/Common/StdHeaders.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct TagIndexTestInternal
    {
        /** @brief 색인의 답(`TagIndex::forEachObjectWithTags`)과 전 오브젝트를 훑은 답(`hasAllTags` — 하위 태그 인정)이 같으면 true 입니다. */
        static bool matchesFullScan( const GameObjectManager& manager, const TagContainer& required )
        {
            vector<uint64> listIndexed;
            manager.getTagIndex().forEachObjectWithTags( required, [&listIndexed]( GameObject* pObject )
            {
                listIndexed.push_back( pObject->getObjectID() );
            } );
            vector<uint64> listScanned;
            manager.forEachGameObject( [&listScanned, &required]( GameObject* pObject )
            {
                if ( required.getTagCount() > 0 && pObject->getTags().hasAllTags( required ) )
                    listScanned.push_back( pObject->getObjectID() );
            } );
            std::sort( listIndexed.begin(), listIndexed.end() );
            std::sort( listScanned.begin(), listScanned.end() );
            return listIndexed == listScanned;
        }

        /** @brief 색인이 @p required 로 방문한 오브젝트 수입니다. */
        static uint32 countIndexed( const GameObjectManager& manager, const TagContainer& required )
        {
            uint32 count = 0;
            manager.getTagIndex().forEachObjectWithTags( required, [&count]( GameObject* /*pObject*/ )
            {
                ++count;
            } );
            return count;
        }

        /** @brief 질의 묶음 모두에서 색인과 전 훑기가 같은지 봅니다. */
        static bool allQueriesMatchFullScan( const GameObjectManager& manager )
        {
            const TagContainer arrQuery[] = {
                TagContainer{ "Faction"_tag },
                TagContainer{ "Faction.Enemy"_tag },
                TagContainer{ "Faction.Player"_tag },
                TagContainer{ "Faction.Enemy.Boss"_tag },
                TagContainer{ "Factional"_tag },
                TagContainer{ "Other"_tag },
                TagContainer{ "Faction"_tag, "Role"_tag },
                TagContainer{ TagID::request( "faction.player" ) },
                TagContainer{},
            };
            bool bAllMatch = true;
            for ( const TagContainer& query : arrQuery )
            {
                bAllMatch = matchesFullScan( manager, query ) && bAllMatch;
            }
            return bAllMatch;
        }
    };
} // namespace

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
 * @details `""_tag` · `TagID::request` · 에디터 Hierarchy 의 `tag:` 필터가 모두 `TagID::computeID` 를 쓴다. 한 곳이 대소문자 규칙을
 *          달리하면(예: `computeStringHash64` 를 기본 인자로 불러 무시) 대문자로 시작하는 태그(`Collider` · `Sprite` · `UI` · `Faction.Player` …)를
 *          **하나도 찾지 못한다.**
 */
SW_TEST_CASE( TagSystemTest, IDBuiltFromStringFindsLiteralTag )
{
    TagContainer owned{ "Collider"_tag, "Faction.Player"_tag };

    // 필터가 하는 일 그대로 — 문자열만 들고 ID 를 만들어 묻는다.
    const string_view exact{ "Collider" };
    SW_EXPECT_TRUE( owned.hasTag( TagID{ TagID::computeID( exact.data(), exact.size() ), exact.data() } ) );

    // 대소문자는 무시한다. intern 이 이미 무시하므로 ID 도 같아야 앞뒤가 맞는다.
    const string_view lowered{ "collider" };
    SW_EXPECT_TRUE( owned.hasTag( TagID{ TagID::computeID( lowered.data(), lowered.size() ), lowered.data() } ) );

    // 계층도 잡힌다 — 문자열을 함께 넘기므로 isSubtagOf 가 돈다.
    const string_view parent{ "Faction" };
    SW_EXPECT_TRUE( owned.hasTag( TagID{ TagID::computeID( parent.data(), parent.size() ), parent.data() } ) );

    // 없는 태그를 찾아내면 안 된다.
    const string_view absent{ "Sprite" };
    SW_EXPECT_FALSE( owned.hasTag( TagID{ TagID::computeID( absent.data(), absent.size() ), absent.data() } ) );
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
    uint64 rawID       = originalTag._id;

    // 2) 역직렬화 모의: 숫자 ID만 가진 TagID 복원
    TagID restoredTag( rawID );
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

// ------------------------------------------------------------------------------
// 3) TagIndexTest — 태그 → 오브젝트 색인이 계층 태그 의미(`hasTag( tag, false )`)를 똑같이 지킨다
// ------------------------------------------------------------------------------
/**
 * @brief [TagIndexTest] 부모 태그 질의 · 대소문자 · 접두어만 같은 태그 · 여러 태그 질의가 전 오브젝트 훑기와 같은 답이고, 태그 빼기 · 비우기 · 파괴 · 컴포넌트 떼기를 따라간다
 */
SW_TEST_CASE( TagIndexTest, HierarchicalQueryMatchesFullScanThroughEveryWrite )
{
    using Internal = TagIndexTestInternal;
    GameObjectManager manager;
    GameObject*       pPlayer = manager.createGameObject( hashed_string( "Player" ) );
    GameObject*       pBoss   = manager.createGameObject( hashed_string( "Boss" ) );
    GameObject*       pLower  = manager.createGameObject( hashed_string( "Lower" ) );
    GameObject*       pPrefix = manager.createGameObject( hashed_string( "Prefix" ) );
    GameObject*       pNone   = manager.createGameObject( hashed_string( "None" ) );
    SW_ASSERT_TRUE( pPlayer != nullptr && pBoss != nullptr && pLower != nullptr && pPrefix != nullptr && pNone != nullptr );
    pPlayer->addTag( "Faction.Player"_tag );
    pPlayer->addTag( "Role.Scout"_tag );
    pBoss->addTag( "Faction.Enemy.Boss"_tag );
    pLower->addTag( TagID::request( "faction" ) ); // 대소문자만 다른 부모 태그 — 같은 태그다
    pPrefix->addTag( "Factional"_tag );            // 접두어만 같다 — `Faction` 의 하위 태그가 아니다

    SW_EXPECT_TRUE( Internal::allQueriesMatchFullScan( manager ) );
    SW_EXPECT_EQUAL( 3u, Internal::countIndexed( manager, TagContainer{ "Faction"_tag } ) );
    SW_EXPECT_EQUAL( 1u, Internal::countIndexed( manager, TagContainer{ "Faction.Enemy"_tag } ) );
    SW_EXPECT_EQUAL( 1u, Internal::countIndexed( manager, TagContainer{ "Faction"_tag, "Role"_tag } ) );
    SW_EXPECT_EQUAL( 0u, Internal::countIndexed( manager, TagContainer{} ) );

    // 같은 태그를 다시 더해도 칸은 그대로다(세대도 오르지 않는다).
    const uint64 generationBefore = manager.getTagIndex().getGeneration();
    pBoss->addTag( "Faction.Enemy.Boss"_tag );
    SW_EXPECT_EQUAL( generationBefore, manager.getTagIndex().getGeneration() );

    pPlayer->removeTag( "Faction.Player"_tag );
    pBoss->clearTags();
    SW_EXPECT_TRUE( Internal::allQueriesMatchFullScan( manager ) );
    SW_EXPECT_EQUAL( 1u, Internal::countIndexed( manager, TagContainer{ "Faction"_tag } ) );
    SW_EXPECT_EQUAL( 1u, Internal::countIndexed( manager, TagContainer{ "Role"_tag } ) );

    // 파괴 표시만으로 빠지고(삭제 대기), 해체 뒤에도 같다.
    manager.destroyObject( pLower );
    SW_EXPECT_TRUE( Internal::allQueriesMatchFullScan( manager ) );
    SW_EXPECT_EQUAL( 0u, Internal::countIndexed( manager, TagContainer{ "Faction"_tag } ) );
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( Internal::allQueriesMatchFullScan( manager ) );

    // 태그 컴포넌트를 떼면 그 칸이 모두 빠진다.
    TagComponent* pPlayerTags = pPlayer->getComponent<TagComponent>();
    SW_ASSERT_NOT_NULL( pPlayerTags );
    SW_EXPECT_TRUE( pPlayer->removeComponent( pPlayerTags ) );
    SW_EXPECT_EQUAL( 0u, Internal::countIndexed( manager, TagContainer{ "Role"_tag } ) );
    SW_EXPECT_TRUE( Internal::allQueriesMatchFullScan( manager ) );
}

// ------------------------------------------------------------------------------
// 4) StructureGenerationTest — 구조 세대는 구조를 바꾸는 세터에서만 오르고 매니저끼리 겹치지 않는다
// ------------------------------------------------------------------------------
/**
 * @brief [StructureGenerationTest] 생성 · 이름 · 형제 순서 · 부착과 떼기 · 컴포넌트 · 파괴 표시가 세대를 올리고, 같은 값 쓰기와 태그는 올리지 않는다
 */
SW_TEST_CASE( StructureGenerationTest, StructuralSettersBumpAndOthersDoNot )
{
    GameObjectManager manager;
    uint64            generation = manager.getStructureGeneration();
    SW_EXPECT_TRUE( generation != 0 );

    GameObject* pParent = manager.createGameObject( hashed_string( "Parent" ) );
    GameObject* pChild  = manager.createGameObject( hashed_string( "Child" ) );
    SW_ASSERT_TRUE( pParent != nullptr && pChild != nullptr );
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );

    generation = manager.getStructureGeneration();
    SW_ASSERT_NOT_NULL( pParent->addComponent<SceneComponent>() );
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );
    SW_ASSERT_NOT_NULL( pChild->addComponent<SceneComponent>() );

    generation = manager.getStructureGeneration();
    pChild->setName( hashed_string( "Renamed" ) );
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );

    generation = manager.getStructureGeneration();
    pChild->setSiblingIndex( pChild->getSiblingIndex() ); // 같은 값
    SW_EXPECT_EQUAL( generation, manager.getStructureGeneration() );
    pChild->setSiblingIndex( 3 );
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );

    generation = manager.getStructureGeneration();
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );
    generation = manager.getStructureGeneration();
    pChild->detachFromParent();
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );

    // 태그는 구조가 아니다(태그 색인의 세대가 따로 오른다). 첫 태그는 태그 컴포넌트를 붙이므로 그 뒤 태그로 본다.
    pChild->addTag( "Faction.Player"_tag );
    generation                    = manager.getStructureGeneration();
    const uint64 tagGenerationOld = manager.getTagIndex().getGeneration();
    pChild->addTag( "Role.Scout"_tag );
    SW_EXPECT_EQUAL( generation, manager.getStructureGeneration() );
    SW_EXPECT_TRUE( manager.getTagIndex().getGeneration() != tagGenerationOld );

    manager.destroyObject( pChild );
    SW_EXPECT_TRUE( manager.getStructureGeneration() != generation );

    // 다른 매니저의 세대와 겹치지 않는다(지운 매니저 자리에 새 매니저가 생겨도 캐시가 틀리지 않는다).
    GameObjectManager other;
    SW_EXPECT_TRUE( other.getStructureGeneration() != manager.getStructureGeneration() );
}
