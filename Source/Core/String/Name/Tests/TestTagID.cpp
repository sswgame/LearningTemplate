// TagIDTest — 태그 id 규칙: 리터럴 · 런타임 요청이 같은 id(대소문자 무시), 점 계층의 조상 판정 경계, id 만 든 태그의 이름 역조회.
// 이름 풀(hashed_string) 자체의 규칙은 StringTest.HashedStringFollowsFNameRules 가 본다.
#include "pch.h"

#include "Core/String/Name/TagID.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/** @brief [TagIDTest] 리터럴과 런타임 요청은 같은 함수로 id 를 내고, 대소문자만 다른 철자는 같은 태그다 */
SW_TEST_CASE( TagIDTest, LiteralAndRequestShareOneCaseInsensitiveID )
{
    // 틀리면: 리터럴 · 요청이 다른 해시를 쓰거나 한쪽만 대소문자를 구별하면 같음 단언이, 빈 태그가 유효하면 isValid 단언이 진다.
    constexpr TagID literal = "weapon.rifle"_tag;
    SW_EXPECT_TRUE( literal == TagID::request( "weapon.rifle" ) );
    SW_EXPECT_TRUE( literal == TagID::request( "Weapon.Rifle" ) );
    SW_EXPECT_TRUE( literal != TagID::request( "weapon.rifle2" ) );
    SW_EXPECT_TRUE( literal.isValid() );
    SW_EXPECT_FALSE( TagID{}.isValid() );
}

/** @brief [TagIDTest] 조상은 '.' 경계에서만이다: 자신 · 부모 · 조부모는 참, 이름 앞부분만 같거나 더 긴 부모는 거짓, 대소문자는 무시한다 */
SW_TEST_CASE( TagIDTest, SubtagStopsAtDotBoundaries )
{
    // 틀리면: '.' 경계를 보지 않으면 "weapon.rifleman" 단언이, 부모가 더 길 때 끝을 넘으면 "weapon" 의 자식 단언이, 대소문자를 구별하면 "Weapon" 단언이 진다.
    const TagID rifle = TagID::request( "weapon.rifle.scoped" );
    SW_EXPECT_TRUE( rifle.isSubtagOf( rifle ) );
    SW_EXPECT_TRUE( rifle.isSubtagOf( TagID::request( "weapon.rifle" ) ) );
    SW_EXPECT_TRUE( rifle.isSubtagOf( TagID::request( "weapon" ) ) );
    SW_EXPECT_TRUE( rifle.isSubtagOf( TagID::request( "Weapon" ) ) );
    SW_EXPECT_FALSE( TagID::request( "weapon.rifleman" ).isSubtagOf( TagID::request( "weapon.rifle" ) ) );
    SW_EXPECT_FALSE( TagID::request( "weapon" ).isSubtagOf( TagID::request( "weapon.rifle" ) ) );
    SW_EXPECT_FALSE( TagID::request( "weapons" ).isSubtagOf( TagID::request( "weapon" ) ) );
}

/** @brief [TagIDTest] id 만 든 태그(저장된 id)는 요청된 적이 있으면 이름을 되찾고, 모르는 id 는 이름도 조상도 없다 */
SW_TEST_CASE( TagIDTest, IDOnlyTagFindsItsNameThroughTheRegistry )
{
    // 틀리면: 요청이 등록하지 않으면 getString 이 nullptr 이라 REQUIRE 가, 모르는 id 에 이름을 지어내면 nullptr 단언이 진다.
    const TagID requested = TagID::request( "tagidtest.burning" );
    const TagID idOnly{ requested._id };
    const utf8* pName = idOnly.getString();
    SW_REQUIRE_TRUE( pName != nullptr );
    SW_EXPECT_EQUAL( string( "tagidtest.burning" ), string( pName ) );
    SW_EXPECT_TRUE( idOnly.isSubtagOf( TagID::request( "tagidtest" ) ) );

    const TagID unknown{ TagID::computeID( "never.requested.tag", 19 ) };
    SW_EXPECT_TRUE( unknown.getString() == nullptr );
    SW_EXPECT_FALSE( unknown.isSubtagOf( TagID::request( "never" ) ) );
}
