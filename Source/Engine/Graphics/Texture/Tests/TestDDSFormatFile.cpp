// DDSFormatFile — DDS 형식 이름 파일(`Config/Editor/DdsFormats.json`)이 C++ 표와 같은지 본다: 로더의 옛 FourCC · RGB 마스크 표(두 방향),
// 로더의 크기 표와 이름 목록(두 방향), Texture2D 가 읽는 DXGI 번호는 모두 이름 · 크기가 있다. 파일은 파이썬 에셋 검증이 읽는 이름표의 정본이다(nogpu).
// 임포터 표는 편집기 케이스(`EditorTexturePipelineTest.DDSFormatFileMatchesTheImportTable`)가 본다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

using namespace sw;

namespace
{
    /** @brief "0x…" 16 진 글을 읽습니다. 틀린 글이면 false 입니다. */
    [[nodiscard]] bool readHex( const JSONValue& value, uint32& outNumber )
    {
        const string text = value.asString();
        if ( text.size() < 3 || text[0] != '0' || ( text[1] != 'x' && text[1] != 'X' ) )
            return false;
        uint32 number = 0;
        for ( size_t index = 2; index < text.size(); ++index )
        {
            const utf8 letter = text[index];
            uint32     digit  = 0;
            if ( letter >= '0' && letter <= '9' )
                digit = static_cast<uint32>( letter - '0' );
            else if ( ( letter | 0x20 ) >= 'a' && ( letter | 0x20 ) <= 'f' )
                digit = static_cast<uint32>( ( letter | 0x20 ) - 'a' + 10 );
            else
                return false;
            number = ( number << 4 ) | digit;
        }
        outNumber = number;
        return true;
    }

    /** @brief `dxgi` 칸을 읽습니다(없거나 틀리면 0 — DXGI_FORMAT_UNKNOWN 이라 어떤 줄과도 맞지 않는다). */
    uint32 readDxgi( const JSONValue& row )
    {
        const std::optional<uint32> dxgi = row.get( "dxgi" ).read<uint32>();
        return dxgi.has_value() ? *dxgi : 0u;
    }
} // namespace

/**
 * @brief [DDSFormatFileTest] DDS 형식 파일이 로더의 옛 머리 표 · 크기 표와 같고, Texture2D 가 읽는 번호는 모두 이름이 있다
 * @details 파이썬은 C++ 를 읽지 못하고 C++ 에서 만든 JSON 은 빌드가 있어야 해서 데이터 파일 하나를 둘이 본다. 표 하나를 고치고 파일을 잊으면 여기서 진다.
 */
SW_TEST_CASE( DDSFormatFileTest, MatchesTheLoaderTables )
{
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Editor/DdsFormats.json" ), text ) );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( text, "DdsFormats.json" ) );
    const JSONValue formats = document.getRoot().get( "dxgi_formats" );
    const JSONValue legacy  = document.getRoot().get( "legacy_fourcc" );
    const JSONValue masks   = document.getRoot().get( "legacy_rgb_masks" );
    SW_REQUIRE_TRUE( formats.isArray() && legacy.isArray() && masks.isArray() );

    const auto hasName = [&formats]( uint32 dxgiFormat )
    {
        for ( size_t index = 0; index < formats.size(); ++index )
        {
            if ( readDxgi( formats.at( index ) ) == dxgiFormat && formats.at( index ).get( "name" ).asString().empty() == false )
                return true;
        }
        return false;
    };

    // 크기 표 ↔ 이름 목록: 줄 수가 같고 로더의 번호가 모두 파일에 있다
    const vector<DDSFormatSize> listSize = DDSLoader::getFormatSizeTable();
    SW_EXPECT_EQUAL( listSize.size(), formats.size() );
    const auto hasSize = [&listSize]( uint32 dxgiFormat )
    {
        for ( const DDSFormatSize& row : listSize )
        {
            if ( row._dxgiFormat == dxgiFormat )
                return true;
        }
        return false;
    };
    for ( const DDSFormatSize& row : listSize )
    {
        SW_EXPECT_TRUE_MSG( hasName( row._dxgiFormat ), "DDSLoader reads DXGI " + to_string( row._dxgiFormat ) + " but DdsFormats.json has no name for it" );
    }

    // 옛 FourCC 표 — 네 글자 코드(fourcc) 또는 D3DFMT 정수(d3dfmt)
    const vector<DDSLegacyFourCC> listFourCC = DDSLoader::getLegacyFourCCTable();
    SW_EXPECT_EQUAL( listFourCC.size(), legacy.size() );
    for ( const DDSLegacyFourCC& loaderRow : listFourCC )
    {
        bool bFound = false;
        for ( size_t index = 0; index < legacy.size() && bFound == false; ++index )
        {
            const JSONValue row  = legacy.at( index );
            uint32          code = 0;
            if ( row.has( "fourcc" ) )
            {
                const string letters = row.get( "fourcc" ).asString();
                SW_REQUIRE_TRUE_MSG( letters.size() == 4, "fourcc must be four letters: " + letters );
                code = static_cast<uint32>( static_cast<uint8>( letters[0] ) ) | ( static_cast<uint32>( static_cast<uint8>( letters[1] ) ) << 8 ) |
                       ( static_cast<uint32>( static_cast<uint8>( letters[2] ) ) << 16 ) | ( static_cast<uint32>( static_cast<uint8>( letters[3] ) ) << 24 );
            }
            else
            {
                const std::optional<uint32> d3dFormat = row.get( "d3dfmt" ).read<uint32>();
                SW_REQUIRE_TRUE_MSG( d3dFormat.has_value(), "a legacy_fourcc row has neither fourcc nor d3dfmt" );
                code = *d3dFormat;
            }
            bFound = code == loaderRow._fourCC && readDxgi( row ) == loaderRow._dxgiFormat;
        }
        SW_EXPECT_TRUE_MSG( bFound, "DDSLoader FourCC row " + to_string( loaderRow._fourCC ) + " -> " + to_string( loaderRow._dxgiFormat ) + " is not in DdsFormats.json" );
    }

    // 옛 RGB 마스크 표 — 다섯 칸이 모두 같아야 맞는다
    const vector<DDSLegacyRGBMask> listMask = DDSLoader::getLegacyRGBMaskTable();
    SW_EXPECT_EQUAL( listMask.size(), masks.size() );
    for ( const DDSLegacyRGBMask& loaderRow : listMask )
    {
        bool bFound = false;
        for ( size_t index = 0; index < masks.size() && bFound == false; ++index )
        {
            const JSONValue row = masks.at( index );
            uint32          arrMask[4]{};
            const bool      bRead = readHex( row.get( "r" ), arrMask[0] ) && readHex( row.get( "g" ), arrMask[1] ) && readHex( row.get( "b" ), arrMask[2] ) &&
                               readHex( row.get( "a" ), arrMask[3] );
            SW_REQUIRE_TRUE_MSG( bRead, "a legacy_rgb_masks row has a mask that is not 0x hex" );
            bFound = row.get( "bits" ).read<uint32>() == loaderRow._bitCount && arrMask[0] == loaderRow._rMask && arrMask[1] == loaderRow._gMask &&
                     arrMask[2] == loaderRow._bMask && arrMask[3] == loaderRow._aMask && readDxgi( row ) == loaderRow._dxgiFormat;
        }
        SW_EXPECT_TRUE_MSG( bFound, "DDSLoader RGB mask row -> DXGI " + to_string( loaderRow._dxgiFormat ) + " is not in DdsFormats.json" );
    }

    // Texture2D 가 읽는 번호는 모두 이름 · 크기가 있다(DXGI 번호는 0 ~ 132 안이다) — 이름이 없으면 검증이 `DXGI_<번호>` 로 적고 규칙이 그 형식을 적을 길이 없다
    for ( uint32 dxgiFormat = 1; dxgiFormat <= 132; ++dxgiFormat )
    {
        if ( Texture2D::toRHIFormatFromDxgi( dxgiFormat ) == RHIFormat::Unknown )
            continue;
        SW_EXPECT_TRUE_MSG( hasName( dxgiFormat ), "Texture2D reads DXGI " + to_string( dxgiFormat ) + " but DdsFormats.json has no name for it" );
        SW_EXPECT_TRUE_MSG( hasSize( dxgiFormat ), "Texture2D reads DXGI " + to_string( dxgiFormat ) + " but DDSLoader has no block size for it" );
    }
}
