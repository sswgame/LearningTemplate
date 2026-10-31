#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Serialization/XML/XMLDocument.h"

// MaterialXMLTest — 저장소의 실제 .material 을 읽어 프로퍼티 깃발(bSrgb · bHdr)이 데이터대로인지(nogpu).
// 약어 일괄 변경(1dafbae0c9)이 읽기 철자를 bSRGB · bHDR 로 바꿔 데이터의 bSrgb="0" 이 조용히 기본값(true)이 되었다 — 스플랫 · 노멀 맵이 sRGB 로 읽혔다.

namespace
{
    bool findProperty( const sw::string& relativePath, const utf8* pName, sw::MaterialProperty& outProp )
    {
        sw::string text;
        if ( sw::FileUtil::readTextFile( sw::ProcessRoots::get()._project.str() + "/" + relativePath, text ) == false )
            return false;
        sw::XMLDocument doc;
        if ( doc.parse( text ) == false )
            return false;
        for ( sw::XMLNode item = doc.getRoot( "MaterialDesc" ).findChild( "_properties" ).findChild( "item" ); item; item = item.findNextSibling( "item" ) )
        {
            sw::MaterialProperty prop{};
            if ( sw::MaterialXML::parsePropertyNode( item, prop ) == false )
                return false;
            if ( prop._name == pName )
            {
                outProp = prop;
                return true;
            }
        }
        return false;
    }
} // namespace

/**
 * @brief [MaterialXMLTest] 실제 머티리얼의 bSrgb="0" · bHdr="1" 이 그대로 읽힌다
 */
SW_TEST_CASE( MaterialXMLTest, ReadsSrgbAndHdrFlagsOfRealMaterials )
{
    sw::MaterialProperty splat{};
    SW_REQUIRE_TRUE( findProperty( "Content/engine/materials/terrain.material", "_splatMap", splat ) );
    SW_EXPECT_TRUE( splat._bSRGB == 0 );

    sw::MaterialProperty emissive{};
    SW_REQUIRE_TRUE( findProperty( "Content/engine/materials/toon.material", "_emissiveColor", emissive ) );
    SW_EXPECT_TRUE( emissive._bHDR == 1 );
    SW_EXPECT_TRUE( emissive._bSRGB == 1 );
}

/**
 * @brief [MaterialXMLTest] 모르는 속성(철자가 다른 bSRGB)은 기본값이 되지 않고 읽기 실패다
 */
SW_TEST_CASE( MaterialXMLTest, RejectsMisspelledPropertyAttribute )
{
    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( "<item name=\"_albedoMap\" type=\"Texture2D\" bSRGB=\"0\"/>" ) );
    sw::MaterialProperty prop{};
    SW_EXPECT_LOG( "Xml.UnknownAttribute" );
    SW_EXPECT_FALSE( sw::MaterialXML::parsePropertyNode( doc.getRoot( "item" ), prop ) );
}
