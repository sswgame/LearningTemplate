// XMLSerializer 의 루트 검사 — 이름이 다른 루트는 어느 입구로도 읽히지 않고(`Xml.WrongRoot`), 부르는 쪽이 고른 요소는 이름과 상관없이 읽힌다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Visual/MeshLODSettings.h"

using namespace sw;

namespace
{
    struct XTestUtil
    {
        static constexpr string_view kWrongRoot = "<OtherKind _refineHysteresis=\"0.5\" _skinnedBoundsScale=\"2\" />";
        static constexpr string_view kRightRoot = "<MeshLODSettings _refineHysteresis=\"0.5\" _skinnedBoundsScale=\"2\" />";
    };
} // namespace

/** @brief [XMLSerializerRootTest] 칸 이름이 다 맞아도 루트 이름이 다르면 읽지 않고 값을 건드리지 않는다 */
SW_TEST_CASE( XMLSerializerRootTest, WrongRootIsRefusedByEveryEntry )
{
    SW_EXPECT_LOG( "Xml.WrongRoot" );
    MeshLODSettings settings;

    SW_EXPECT_FALSE( XMLSerializer::deserialize( &settings, *MeshLODSettings::StaticType(), XTestUtil::kWrongRoot ) );
    vector<SchemaOrphanValue> listOrphan;
    SW_EXPECT_TRUE( XMLSerializer::deserializeSoft( &settings, *MeshLODSettings::StaticType(), XTestUtil::kWrongRoot, &listOrphan ) == SoftReadResult::NotParsed );
    XMLDocumentBackend backend;
    SW_EXPECT_FALSE( XMLSerializer::deserialize( &settings, *MeshLODSettings::StaticType(), backend, XTestUtil::kWrongRoot ) );
    SW_EXPECT_FALSE( ConfigManager::readConfigXMLDocument( &settings, *MeshLODSettings::StaticType(), XTestUtil::kWrongRoot, "wrong root" ) );
    SW_EXPECT_EQUAL( 0.0f, settings._refineHysteresis );
    SW_EXPECT_EQUAL( 0.0f, settings._skinnedBoundsScale );
}

/** @brief [XMLSerializerRootTest] 이름이 맞는 루트는 세 입구 모두 읽는다 */
SW_TEST_CASE( XMLSerializerRootTest, RightRootReads )
{
    MeshLODSettings fromStrict;
    SW_EXPECT_TRUE( XMLSerializer::deserialize( &fromStrict, *MeshLODSettings::StaticType(), XTestUtil::kRightRoot ) );
    SW_EXPECT_EQUAL( 2.0f, fromStrict._skinnedBoundsScale );

    MeshLODSettings    fromBackend;
    XMLDocumentBackend backend;
    SW_EXPECT_TRUE( XMLSerializer::deserialize( &fromBackend, *MeshLODSettings::StaticType(), backend, XTestUtil::kRightRoot ) );
    SW_EXPECT_EQUAL( 0.5f, fromBackend._refineHysteresis );

    MeshLODSettings fromConfig;
    SW_EXPECT_TRUE( ConfigManager::readConfigXMLDocument( &fromConfig, *MeshLODSettings::StaticType(), XTestUtil::kRightRoot, "right root" ) );
    SW_EXPECT_EQUAL( 2.0f, fromConfig._skinnedBoundsScale );
}

/** @brief [XMLSerializerRootTest] 부르는 쪽이 이름으로 고른 요소는 요소 이름이 타입 이름이 아니어도 읽는다(`<Engine>` 의 모양). 모르는 칸은 그대로 오류다 */
SW_TEST_CASE( XMLSerializerRootTest, ChosenElementReadsWhateverItsName )
{
    XMLDocument document;
    SW_REQUIRE_TRUE( document.parse( "<Pack><Render _refineHysteresis=\"0.5\" _skinnedBoundsScale=\"2\" /><Typo _refineHysteresi=\"1\" _skinnedBoundsScale=\"2\" /></Pack>" ) );
    const XMLNode root = document.getRoot( "Pack" );

    MeshLODSettings settings;
    SW_EXPECT_TRUE( ConfigManager::readConfigXMLElement( &settings, *MeshLODSettings::StaticType(), root.findChild( "Render" ), "element" ) );
    SW_EXPECT_EQUAL( 0.5f, settings._refineHysteresis );

    SW_EXPECT_LOG( "Config.KeyUnknown" );
    MeshLODSettings typo;
    SW_EXPECT_FALSE( ConfigManager::readConfigXMLElement( &typo, *MeshLODSettings::StaticType(), root.findChild( "Typo" ), "typo" ) );

    vector<SchemaOrphanValue> listOrphan;
    MeshLODSettings           missing;
    SW_EXPECT_TRUE( XMLSerializer::deserializeElementSoft( &missing, *MeshLODSettings::StaticType(), root.findChild( "NoSuchElement" ), &listOrphan ) == SoftReadResult::NotParsed );
}
