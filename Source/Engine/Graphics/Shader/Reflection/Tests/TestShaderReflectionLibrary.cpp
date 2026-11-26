#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

using namespace sw;

// 리플렉션 매니페스트(ShaderReflectionLibrary) — 손으로 채운 리플렉션을 쓰고 다시 읽는다. 컴파일러 없음.

namespace
{
    struct ShaderReflectionLibraryTestUtil
    {
        static ShaderReflectionData makeReflection( string_view bufferName )
        {
            ShaderReflectionData reflection;
            ShaderBufferInfo     buffer;
            buffer._name      = string( bufferName );
            buffer._totalSize = 64;
            reflection._listConstantBuffer.push_back( std::move( buffer ) );
            reflection._groupSize._x = 8;
            return reflection;
        }
    };
} // namespace

/**
 * @brief [ShaderReflectionLibraryTest] 내용이 같은 리플렉션은 파일에 한 번만 적히고, 읽은 항목은 한 벌을 함께 쓰며, 넣은 순서와 무관하게 같은 바이트다
 */
SW_TEST_CASE( ShaderReflectionLibraryTest, EqualReflectionsAreWrittenOnce )
{
    using Util = ShaderReflectionLibraryTestUtil;
    ShaderReflectionLibrary::EntryMap mapEntry;
    mapEntry.emplace( "b_ps.dxil", sw::make_shared<const ShaderReflectionData>( Util::makeReflection( "PassCB" ) ) );
    mapEntry.emplace( "a_vs.dxil", sw::make_shared<const ShaderReflectionData>( Util::makeReflection( "PassCB" ) ) );
    mapEntry.emplace( "c_cs.dxil", sw::make_shared<const ShaderReflectionData>( Util::makeReflection( "Other" ) ) );
    const AbsolutePath folder = test::makeScratchPath( "reflection_manifest" );
    SW_REQUIRE_TRUE( ShaderReflectionLibrary::save( mapEntry, folder ) );
    const AbsolutePath path = folder.join( ShaderReflectionLibrary::getManifestFileName() );
    vector<uint8>      firstBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, firstBytes ) );

    ShaderReflectionLibrary::EntryMap mapLoaded;
    SW_REQUIRE_TRUE( ShaderReflectionLibrary::loadManifestFile( path, mapLoaded ) );
    SW_REQUIRE_TRUE( mapLoaded.size() == 3 );
    SW_EXPECT_TRUE( mapLoaded["a_vs.dxil"] == mapLoaded["b_ps.dxil"] );
    SW_EXPECT_TRUE( mapLoaded["a_vs.dxil"] != mapLoaded["c_cs.dxil"] );
    SW_EXPECT_EQUAL( string( "Other" ), mapLoaded["c_cs.dxil"]->_listConstantBuffer.front()._name );
    SW_EXPECT_EQUAL( 8u, mapLoaded["c_cs.dxil"]->_groupSize._x );

    SW_REQUIRE_TRUE( ShaderReflectionLibrary::save( mapLoaded, folder ) );
    vector<uint8> secondBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, secondBytes ) );
    SW_EXPECT_TRUE( firstBytes == secondBytes );
}

/** @brief [ShaderReflectionLibraryTest] 잘린 매니페스트는 읽기를 거절하고 빈 항목을 남긴다 */
SW_TEST_CASE( ShaderReflectionLibraryTest, TruncatedManifestIsRejected )
{
    using Util = ShaderReflectionLibraryTestUtil;
    ShaderReflectionLibrary::EntryMap mapEntry;
    mapEntry.emplace( "a_vs.dxil", sw::make_shared<const ShaderReflectionData>( Util::makeReflection( "PassCB" ) ) );
    const AbsolutePath folder = test::makeScratchPath( "reflection_manifest" );
    SW_REQUIRE_TRUE( ShaderReflectionLibrary::save( mapEntry, folder ) );
    const AbsolutePath path = folder.join( ShaderReflectionLibrary::getManifestFileName() );
    vector<uint8>      bytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, bytes ) );
    SW_REQUIRE_TRUE( FileUtil::writeFile( path, bytes.data(), bytes.size() - 1 ) );
    SW_EXPECT_LOG( "ShaderReflectionLibrary.ReflectionManifestCorrupt" );
    SW_EXPECT_FALSE( ShaderReflectionLibrary::loadManifestFile( path, mapEntry ) );
    SW_EXPECT_TRUE( mapEntry.empty() );
}
