#include "pch.h"

#include "Editor/Panels/Inspector/InspectorFavorites.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [InspectorFavoritesTest] 키를 넣고 빼고 뒤집으며, 텍스트로 쓰고 다시 읽어도 같은 집합이다
 * @details 줄마다 키 하나인 저장 형식은 CR · 공백 · 빈 줄 · 중복을 견뎌야 한다(손으로 고친 파일, 다른 OS 의 줄끝).
 */
SW_TEST_CASE( InspectorFavoritesTest, AddRemoveToggleAndTextRoundTrip )
{
    InspectorFavorites favorites;
    SW_EXPECT_TRUE( favorites.isEmpty() );

    const string keyFov = InspectorFavorites::makeKey( "CameraComponent", "_fov" );
    SW_EXPECT_TRUE( keyFov == "CameraComponent._fov" );
    SW_EXPECT_TRUE( favorites.add( keyFov ) );
    SW_EXPECT_FALSE( favorites.add( keyFov ) ); // 중복은 넣지 않는다
    SW_EXPECT_FALSE( favorites.add( "" ) );
    SW_EXPECT_TRUE( favorites.add( "SpriteComponent._textureName" ) );
    SW_EXPECT_TRUE( favorites.contains( keyFov ) );
    SW_EXPECT_TRUE( favorites.hasAnyForType( "CameraComponent" ) );
    SW_EXPECT_FALSE( favorites.hasAnyForType( "Camera" ) ); // 접두만 같은 타입은 아니다
    SW_EXPECT_FALSE( favorites.hasAnyForType( "MeshComponent" ) );

    SW_EXPECT_FALSE( favorites.toggle( keyFov ) ); // 있으면 뺀다
    SW_EXPECT_FALSE( favorites.contains( keyFov ) );
    SW_EXPECT_TRUE( favorites.toggle( keyFov ) ); // 없으면 넣는다
    SW_EXPECT_FALSE( favorites.remove( "MeshComponent._meshID" ) );

    InspectorFavorites restored;
    restored.fromText( favorites.toText() );
    SW_EXPECT_TRUE( restored.getKeys() == favorites.getKeys() );

    restored.fromText( "  B.y \r\n\r\nA.x\nB.y\n" );
    SW_ASSERT_EQUAL( size_t{ 2 }, restored.getKeys().size() );
    SW_EXPECT_TRUE( restored.getKeys()[0] == "A.x" ); // 정렬된다
    SW_EXPECT_TRUE( restored.getKeys()[1] == "B.y" );
}
