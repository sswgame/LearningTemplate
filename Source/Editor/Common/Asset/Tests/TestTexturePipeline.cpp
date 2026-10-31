// TexturePipeline — 임포터의 형식 표가 DDS 형식 이름 파일(`Config/Editor/DdsFormats.json`)과 로더의 크기 표에 맞는지 본다.
// 로더의 옛 머리 표 · 크기 표와 파일은 엔진 케이스(`DDSFormatFileTest.MatchesTheLoaderTables`)가 본다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Texture/TextureImportConfig.h"

#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorTexturePipelineTest] 임포터가 써 내는 형식마다(UNORM · sRGB) 번호의 이름이 파일에 있고 UNORM 이름은 `getName` 과 같으며, 로더가 그 번호의 크기를 안다
 * @details 임포터가 낸 DDS 를 로더가 읽지 못하거나(크기 표에 없음) 에셋 검증이 `DXGI_<번호>` 로 적는(이름 없음) 일을 여기서 막는다.
 */
SW_TEST_CASE( EditorTexturePipelineTest, DDSFormatFileMatchesTheImportTable )
{
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Editor/DdsFormats.json" ), text ) );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( text, "DdsFormats.json" ) );
    const JSONValue formats = document.getRoot().get( "dxgi_formats" );
    SW_REQUIRE_TRUE( formats.isArray() );

    const auto findName = [&formats]( uint32 dxgiFormat ) -> string
    {
        for ( size_t index = 0; index < formats.size(); ++index )
        {
            if ( formats.at( index ).get( "dxgi" ).read<uint32>() == dxgiFormat )
                return formats.at( index ).get( "name" ).asString();
        }
        return {};
    };
    const vector<DDSFormatSize> listSize = DDSLoader::getFormatSizeTable();
    const auto                  hasSize  = [&listSize]( uint32 dxgiFormat )
    {
        for ( const DDSFormatSize& row : listSize )
        {
            if ( row._dxgiFormat == dxgiFormat )
                return true;
        }
        return false;
    };

    constexpr TextureImportFormat kArrFormat[] = { TextureImportFormat::BC1, TextureImportFormat::BC3, TextureImportFormat::BC4,
                                                   TextureImportFormat::BC5, TextureImportFormat::BC6H, TextureImportFormat::BC7,
                                                   TextureImportFormat::B8G8R8A8, TextureImportFormat::R8G8B8A8, TextureImportFormat::R16G16B16A16Float,
                                                   TextureImportFormat::R8 };
    for ( const TextureImportFormat format : kArrFormat )
    {
        const string name{ TextureImportFormatUtil::getName( format ) };
        SW_EXPECT_TRUE_MSG( findName( TextureImportFormatUtil::getDxgiFormat( format, false ) ) == name, "import table name differs from DdsFormats.json: " + name );
        for ( const bool bSRGB : { false, true } )
        {
            const uint32 dxgiFormat = TextureImportFormatUtil::getDxgiFormat( format, bSRGB );
            SW_EXPECT_TRUE_MSG( findName( dxgiFormat ).empty() == false, "import writes DXGI " + to_string( dxgiFormat ) + " which DdsFormats.json does not name" );
            SW_EXPECT_TRUE_MSG( hasSize( dxgiFormat ), "import writes DXGI " + to_string( dxgiFormat ) + " which DDSLoader cannot size" );
        }
    }
}
