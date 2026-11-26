// PrefabReferenceTextTest — 씬 엔티티(`<entity prefab>`)와 중첩 프리팹(`<PrefabInstance prefab>`)의 프리팹 칸은 타입 있는 참조(`AssetRef<PrefabAsset>`)다:
// GUID 참조를 색인으로 풀어 읽고, 옮기는 동안의 경로 글도 읽으며, 다시 쓰면 GUID 참조로 적는다(GUID 2 단계 — 같은 이름의 칸이 모두 같은 저장 꼴이라 이전 도구가 이름으로 가른다).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

using namespace sw;

namespace
{
    struct PrefabReferenceTextTestUtil
    {
        static constexpr const utf8* kGUIDText = "guid:0000000000000000000000000000b0a1";
        static constexpr const utf8* kPath     = "game/x/prefabs/crate.prefab.xml";

        /** @brief GUID 하나를 @p pPath 로 푸는 색인을 프로세스 표에 겁니다. 돌려준 표(기동이 건 것)를 테스트 끝에 다시 건다. */
        static AssetRegistry installCrate()
        {
            vector<AssetRegistryLayer> listLayer;
            listLayer.push_back( AssetRegistryLayer{ "test", { AssetRegistryEntry{ *AssetGUID::tryParse( "0000000000000000000000000000b0a1" ), kPath } } } );
            return AssetRegistry::install( std::move( *AssetRegistry::merge( listLayer ) ) );
        }

        /** @brief @p xml 의 뿌리 원소를 @p outParams 로 읽어 프리팹 경로를 확인하고, 다시 써서 `prefab` 속성 글을 돌려줍니다. 읽기에 지면 빈 글입니다. */
        template <typename TParams>
        static string readAndWrite( const utf8* xml, TParams& outParams )
        {
            XMLDocument input;
            XMLDocument output;
            const bool  bRead = input.parse( xml ) && XMLFields::readProperties( input.getRoot(), outParams, "test" ) && output.parse( "<out/>" );
            SW_EXPECT_TRUE( bRead );
            if ( bRead == false )
                return string{};
            SW_EXPECT_TRUE( outParams._prefab == kPath );
            XMLFields::writeProperties( output.getRoot(), outParams );
            return string( output.getRoot().getAttributeText( "prefab" ) );
        }
    };
} // namespace

/** @brief [PrefabReferenceTextTest] 씬 엔티티: GUID 참조도 경로 글도 같은 프리팹으로 읽고, 다시 쓰면 GUID 참조다 */
SW_TEST_CASE( PrefabReferenceTextTest, SceneEntityReadsBothAndWritesTheGUID )
{
    using Util             = PrefabReferenceTextTestUtil;
    AssetRegistry previous = Util::installCrate();

    SceneEntityParams fromGUID;
    SW_EXPECT_EQUAL( string( Util::kGUIDText ), Util::readAndWrite( "<entity id=\"1\" prefab=\"guid:0000000000000000000000000000b0a1\"/>", fromGUID ) );
    SceneEntityParams fromPath;
    SW_EXPECT_EQUAL( string( Util::kGUIDText ), Util::readAndWrite( "<entity id=\"1\" prefab=\"game/x/prefabs/crate.prefab.xml\"/>", fromPath ) );

    (void)AssetRegistry::install( std::move( previous ) );
}

/** @brief [PrefabReferenceTextTest] 중첩 프리팹: 같은 규칙이다 */
SW_TEST_CASE( PrefabReferenceTextTest, NestedPrefabReadsBothAndWritesTheGUID )
{
    using Util             = PrefabReferenceTextTestUtil;
    AssetRegistry previous = Util::installCrate();

    PrefabInstanceParams fromGUID;
    SW_EXPECT_EQUAL( string( Util::kGUIDText ), Util::readAndWrite( "<PrefabInstance prefab=\"guid:0000000000000000000000000000b0a1\"/>", fromGUID ) );
    PrefabInstanceParams fromPath;
    SW_EXPECT_EQUAL( string( Util::kGUIDText ), Util::readAndWrite( "<PrefabInstance prefab=\"game/x/prefabs/crate.prefab.xml\"/>", fromPath ) );

    (void)AssetRegistry::install( std::move( previous ) );
}
