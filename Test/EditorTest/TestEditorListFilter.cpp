#include "pch.h"

#include "Editor/Common/Widgets/EditorListFilter.h"

#include "TestFramework/TestFramework.h"

using sw::editor::EditorListFilter;

/**
 * @brief 빈 필터는 전부 통과시킨다 — 손으로 막던 함정을 구조가 막는지 본다.
 * @details 패널마다 있던 `if ( isNullOrEmpty( pFilter ) ) return true;` 가드를 빼먹으면
 *          `stristr( x, "" )` 이 nullptr 을 주므로 목록이 **전부** 사라졌다. 여기서 고정한다.
 */
SW_TEST_CASE( EditorListFilterTest, EmptyFilterPassesEverything )
{
    SW_EXPECT_TRUE( EditorListFilter{ nullptr }.matches( "anything" ) );
    SW_EXPECT_TRUE( EditorListFilter{ "" }.matches( "anything" ) );
    SW_EXPECT_TRUE( EditorListFilter{ "" }.matches( "" ) );

    // 공백만 입력한 것도 "필터를 걸지 않은 것" 으로 본다.
    SW_EXPECT_TRUE( EditorListFilter{ "   " }.matches( "anything" ) );
    SW_EXPECT_FALSE( EditorListFilter{ "   " }.isActive() );
    SW_EXPECT_FALSE( EditorListFilter{ nullptr }.isActive() );
    SW_EXPECT_TRUE( EditorListFilter{ "x" }.isActive() );

    // 빈 필터는 필드가 하나도 없어도 통과한다.
    SW_EXPECT_TRUE( EditorListFilter{ "" }.matchesAny( {} ) );
}

/** @brief 대소문자를 무시하고 부분 일치한다. */
SW_TEST_CASE( EditorListFilterTest, MatchesSubstringIgnoringCase )
{
    const EditorListFilter filter{ "mesh" };

    SW_EXPECT_TRUE( filter.matches( "MeshComponent" ) );
    SW_EXPECT_TRUE( filter.matches( "MESH" ) );
    SW_EXPECT_TRUE( filter.matches( "StaticMeshRenderer" ) ); // 중간 일치
    SW_EXPECT_TRUE( filter.matches( "mesh" ) );               // 정확히 같은 길이

    SW_EXPECT_FALSE( filter.matches( "Sprite" ) );
    SW_EXPECT_FALSE( filter.matches( "" ) );
    SW_EXPECT_FALSE( filter.matches( "mes" ) ); // 필드가 필터보다 짧다
}

/** @brief 앞뒤 공백은 떼고 비교한다 — 검색창에서 흔한 입력이다. */
SW_TEST_CASE( EditorListFilterTest, TrimsSurroundingWhitespace )
{
    const EditorListFilter filter{ "  mesh  " };

    SW_EXPECT_TRUE( filter.isActive() );
    SW_EXPECT_TRUE( filter.getText() == "mesh" );
    SW_EXPECT_TRUE( filter.matches( "MeshComponent" ) );

    // 공백을 떼지 않으면 " mesh " 를 찾으므로 아래가 실패한다.
    SW_EXPECT_TRUE( EditorListFilter{ " mesh" }.matches( "MeshComponent" ) );
}

/** @brief 여러 필드 중 하나만 맞아도 통과한다 — 패널마다 길이가 다른 stristr 체인을 대신한다. */
SW_TEST_CASE( EditorListFilterTest, MatchesAnyAcrossFields )
{
    const EditorListFilter filter{ "player" };

    // 이름은 안 맞지만 설명이 맞는다 (GlobalVariables 의 name/description/module 조합).
    SW_EXPECT_TRUE( filter.matchesAny( { "gv_debugDraw", "Draws the player capsule", "Engine" } ) );
    SW_EXPECT_TRUE( filter.matchesAny( { "PlayerStart", "", "" } ) );
    SW_EXPECT_FALSE( filter.matchesAny( { "gv_debugDraw", "Draws the capsule", "Engine" } ) );

    // 필드가 비어 있어도 안전하다.
    SW_EXPECT_FALSE( filter.matchesAny( { "", "", "" } ) );
    SW_EXPECT_FALSE( filter.matchesAny( {} ) );
}

/**
 * @brief 종단자가 없는 조각에도 안전하다.
 * @details 필드는 `string_view` 라 널 종단이 보장되지 않는다. `stristr` 은 널 종단 문자열만
 *          받으므로 그대로 쓸 수 없고, 그래서 길이를 맞춰 자른 뒤 비교한다.
 */
SW_TEST_CASE( EditorListFilterTest, HandlesNonTerminatedViews )
{
    const sw::string  backing{ "MeshComponentAndMore" };
    const string_view slice{ backing.data(), 4 }; // "Mesh" — 뒤에 종단자가 없다

    SW_EXPECT_TRUE( EditorListFilter{ "mesh" }.matches( slice ) );
    SW_EXPECT_FALSE( EditorListFilter{ "meshcomponent" }.matches( slice ) );
}
