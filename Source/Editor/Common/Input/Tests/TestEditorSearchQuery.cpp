// EditorSearchQueryTest — 통합 런타임 탐색의 칸 필터: `type:` · `tag:` · `is:` 낱말과 나머지 글로 나누기, 칸 필터는 모두 맞아야 하고 칸이 없는 항목은 빠진다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Input/EditorSearchSources.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    EditorSearchItem makeObject( string_view name, std::initializer_list<const utf8*> listFacet )
    {
        EditorSearchItem item{};
        item._source   = EditorSearchSource::SceneObject;
        item._category = "GameObject";
        item._title    = string( name );
        for ( const utf8* pFacet : listFacet )
        {
            item._listFacet.emplace_back( pFacet );
        }
        return item;
    }
} // namespace

/** @brief [EditorSearchQueryTest] `칸:값` 낱말은 칸 필터(칸은 소문자), 나머지는 글 — 앞뒤가 빈 `:` 낱말은 글이다 */
SW_TEST_CASE( EditorSearchQueryTest, ParseSplitsFacetsFromText )
{
    const EditorSearchQuery query = EditorSearchQuery::parse( "Type:Mesh goblin is:inactive camp :x y:" );
    SW_EXPECT_EQUAL( string( "goblin camp :x y:" ), query._text );
    SW_REQUIRE_EQUAL( size_t{ 2 }, query._listFacet.size() );
    SW_EXPECT_EQUAL( string( "type" ), query._listFacet[0].first );
    SW_EXPECT_EQUAL( string( "Mesh" ), query._listFacet[0].second );
    SW_EXPECT_EQUAL( string( "is" ), query._listFacet[1].first );
}

/** @brief [EditorSearchQueryTest] 이름 · 컴포넌트 유형 · 태그 · 상태로 씬 오브젝트를 고르고, 칸이 없는 항목(명령)은 칸 필터가 있으면 빠진다 */
SW_TEST_CASE( EditorSearchQueryTest, FacetsNarrowSceneObjects )
{
    vector<EditorSearchItem> listItem;
    listItem.push_back( makeObject( "Goblin_01", { "type:SkeletalMeshComponent", "tag:Enemy", "is:active" } ) );
    listItem.push_back( makeObject( "Goblin_02", { "type:SkeletalMeshComponent", "tag:Enemy", "is:inactive" } ) );
    listItem.push_back( makeObject( "Crate", { "type:MeshComponent", "is:active" } ) );
    EditorSearchItem command{};
    command._source = EditorSearchSource::Command;
    command._title  = "Goblin spawn command";
    listItem.push_back( command );

    vector<const EditorSearchItem*> listFound;
    EditorSearchSources::filter( { &listItem }, "type:mesh", listFound );
    SW_EXPECT_EQUAL( size_t{ 3 }, listFound.size() ); // 메시 둘 + 스켈레탈 메시 — 명령은 칸이 없어 빠진다

    EditorSearchSources::filter( { &listItem }, "tag:enemy is:inactive", listFound );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listFound.size() );
    SW_EXPECT_EQUAL( string( "Goblin_02" ), listFound[0]->_title );

    EditorSearchSources::filter( { &listItem }, "goblin", listFound );
    SW_EXPECT_EQUAL( size_t{ 3 }, listFound.size() ); // 칸 필터가 없으면 예전 그대로 글로 거른다(명령 포함)

    EditorSearchSources::filter( { &listItem }, "goblin is:active", listFound );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listFound.size() );
    SW_EXPECT_EQUAL( string( "Goblin_01" ), listFound[0]->_title );
}
