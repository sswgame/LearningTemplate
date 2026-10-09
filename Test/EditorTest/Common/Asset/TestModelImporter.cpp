#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImporter.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/MeshFracture.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

// ModelImporterTest — glTF 원본을 `.mesh` 로 임포트한다(좌표계 · 감김 · 노드 변환 · 색 · 스탬프). 시험이 glTF 를 만들어 쓴다.

namespace
{
    struct TestModelImporterInternal
    {
        /** @brief 바이트를 base64 로 씁니다(glTF data URI 용). */
        static sw::string encodeBase64( const uint8* pData, size_t size )
        {
            static constexpr utf8 kArrAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            sw::string            text;
            for ( size_t offset = 0; offset < size; offset += 3 )
            {
                const uint32 byte0 = pData[offset];
                const uint32 byte1 = offset + 1 < size ? pData[offset + 1] : 0u;
                const uint32 byte2 = offset + 2 < size ? pData[offset + 2] : 0u;
                const uint32 bits  = ( byte0 << 16 ) | ( byte1 << 8 ) | byte2;
                text += kArrAlphabet[( bits >> 18 ) & 0x3Fu];
                text += kArrAlphabet[( bits >> 12 ) & 0x3Fu];
                text += offset + 1 < size ? kArrAlphabet[( bits >> 6 ) & 0x3Fu] : '=';
                text += offset + 2 < size ? kArrAlphabet[bits & 0x3Fu] : '=';
            }
            return text;
        }

        /**
         * @brief 삼각형 하나((0,0,0) · (1,0,0) · (0,1,0), glTF 에서 +Z 를 향한 반시계)를 두 노드가 쓰는 glTF 입니다.
         * @details 노드 0 은 변환 없음, 노드 1 은 이동 (2, 3, @p translationZ). 머티리얼 baseColorFactor 는 (0.5, 0.25, 1, 1)이고
         *          노멀이 없어 면 노멀을 받아야 합니다. 같은 메시에 선(LINES) 프리미티브가 하나 더 있어 건너뛰어야 합니다.
         */
        static sw::string makeTriangleGltf( float32 translationZ )
        {
            const float32    arrPosition[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
            const sw::string base64         = encodeBase64( reinterpret_cast<const uint8*>( arrPosition ), sizeof( arrPosition ) );

            sw::string json = R"({ "asset": { "version": "2.0" }, "scene": 0, "scenes": [ { "nodes": [ 0, 1 ] } ],
  "nodes": [ { "mesh": 0 }, { "mesh": 0, "translation": [ 2.0, 3.0, )";
            json += std::to_string( translationZ ).c_str();
            json += R"( ] } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 }, { "attributes": { "POSITION": 0 }, "mode": 1 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorFactor": [ 0.5, 0.25, 1.0, 1.0 ] } } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] } ],
  "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,)";
            json += base64;
            json += R"(" } ] })";
            return json;
        }

        /**
         * @brief 삼각형 하나((0,0,0) · (1,0,0) · (0,1,0))를 쓰는 glTF 입니다. 씬 · 노드는 @p pSceneAndNodes 가 정합니다(메시 0 이 그 삼각형).
         */
        static sw::string makeSceneGltf( const utf8* pSceneAndNodes )
        {
            const float32    arrPosition[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
            const sw::string base64         = encodeBase64( reinterpret_cast<const uint8*>( arrPosition ), sizeof( arrPosition ) );

            sw::string json = R"({ "asset": { "version": "2.0" }, "scene": 0, )";
            json += pSceneAndNodes;
            json += R"(,
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] } ],
  "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,)";
            json += base64;
            json += R"(" } ] })";
            return json;
        }

        /** @brief 메시 노드 하나를 (2, 3, 4) 로 옮긴 glTF 입니다 — 엔진 공간 경계는 X [-3, -2] · Y [3, 4] · Z [4, 4] 입니다. */
        static sw::string makeOffsetTriangleGltf()
        {
            return makeSceneGltf( R"("scenes": [ { "nodes": [ 0 ] } ], "nodes": [ { "mesh": 0, "translation": [ 2.0, 3.0, 4.0 ] } ])" );
        }

        static sw::editor::ModelImportRule makeRule( sw::editor::ModelRecenter recenter, const sw::float3& translation = sw::float3{} )
        {
            sw::editor::ModelImportRule rule;
            rule._recenter          = recenter;
            rule._arrTranslation[0] = translation._x;
            rule._arrTranslation[1] = translation._y;
            rule._arrTranslation[2] = translation._z;
            return rule;
        }

        static void appendUint32( uint32 value, sw::vector<uint8>& inoutBytes )
        {
            for ( uint32 shift = 0; shift < 32; shift += 8 )
            {
                inoutBytes.push_back( static_cast<uint8>( ( value >> shift ) & 0xFFu ) );
            }
        }

        /**
         * @brief 삼각형 하나를 BIN 청크에 담은 GLB 입니다. 씬 뿌리는 표준을 어기고 자식 노드를 적습니다(UniGLTF 모양: 항등 `tmpParent` 아래
         *        (2, 0, 1.5) 로 옮긴 메시 노드). 다시 쓴 JSON 은 길이가 달라 4 바이트 정렬 채우기를 지납니다.
         */
        static sw::vector<uint8> makeChildRootGlb()
        {
            sw::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[1]}],)"
                              R"("nodes":[{"name":"tmpParent","children":[1]},{"name":"model","mesh":0,"translation":[2.0,0.0,1.5]}],)"
                              R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
                              R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],)"
                              R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"byteLength":36}]})";
            while ( json.size() % 4 != 0 )
            {
                json += ' ';
            }
            const float32 arrPosition[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

            sw::vector<uint8> bytes;
            appendUint32( 0x46546C67u, bytes ); // "glTF"
            appendUint32( 2u, bytes );
            appendUint32( static_cast<uint32>( 12 + 8 + json.size() + 8 + sizeof( arrPosition ) ), bytes );
            appendUint32( static_cast<uint32>( json.size() ), bytes );
            appendUint32( 0x4E4F534Au, bytes ); // "JSON"
            bytes.insert( bytes.end(), reinterpret_cast<const uint8*>( json.data() ), reinterpret_cast<const uint8*>( json.data() ) + json.size() );
            appendUint32( static_cast<uint32>( sizeof( arrPosition ) ), bytes );
            appendUint32( 0x004E4942u, bytes ); // "BIN\0"
            bytes.insert( bytes.end(), reinterpret_cast<const uint8*>( arrPosition ), reinterpret_cast<const uint8*>( arrPosition ) + sizeof( arrPosition ) );
            return bytes;
        }

        static sw::float3 getPosition( const sw::RHIVertex& vertex ) { return sw::float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] }; }

        static bool isNear( const sw::float3& expected, const sw::float3& actual )
        {
            return ( expected - actual ).getLength() < 1e-5f;
        }

        /** @brief 삼각형 목록에 @p expected 위치의 정점이 있는지 봅니다. */
        static bool containsPosition( const sw::vector<sw::RHIVertex>& listVertex, const sw::float3& expected )
        {
            for ( const sw::RHIVertex& vertex : listVertex )
            {
                if ( isNear( expected, getPosition( vertex ) ) )
                    return true;
            }
            return false;
        }
    };

    struct TestModelFractureInternal
    {
        static void appendQuad( sw::vector<float32>& inoutList, const sw::float3& a, const sw::float3& b, const sw::float3& c, const sw::float3& d )
        {
            for ( const sw::float3& point : { a, b, c, a, c, d } )
            {
                inoutList.push_back( point._x );
                inoutList.push_back( point._y );
                inoutList.push_back( point._z );
            }
        }

        /** @brief 반 크기 (1, 0.5, 0.25) 의 닫힌 상자(바깥에서 보아 반시계, 인덱스 없음) glTF 입니다. */
        static sw::string makeBoxGltf()
        {
            const sw::float3    lo{ -1.0f, -0.5f, -0.25f };
            const sw::float3    hi{ 1.0f, 0.5f, 0.25f };
            sw::vector<float32> list;
            appendQuad( list, sw::float3{ hi._x, lo._y, lo._z }, sw::float3{ hi._x, hi._y, lo._z }, sw::float3{ hi._x, hi._y, hi._z }, sw::float3{ hi._x, lo._y, hi._z } );
            appendQuad( list, sw::float3{ lo._x, lo._y, hi._z }, sw::float3{ lo._x, hi._y, hi._z }, sw::float3{ lo._x, hi._y, lo._z }, sw::float3{ lo._x, lo._y, lo._z } );
            appendQuad( list, sw::float3{ lo._x, hi._y, lo._z }, sw::float3{ lo._x, hi._y, hi._z }, sw::float3{ hi._x, hi._y, hi._z }, sw::float3{ hi._x, hi._y, lo._z } );
            appendQuad( list, sw::float3{ lo._x, lo._y, hi._z }, sw::float3{ lo._x, lo._y, lo._z }, sw::float3{ hi._x, lo._y, lo._z }, sw::float3{ hi._x, lo._y, hi._z } );
            appendQuad( list, sw::float3{ lo._x, lo._y, hi._z }, sw::float3{ hi._x, lo._y, hi._z }, sw::float3{ hi._x, hi._y, hi._z }, sw::float3{ lo._x, hi._y, hi._z } );
            appendQuad( list, sw::float3{ hi._x, lo._y, lo._z }, sw::float3{ lo._x, lo._y, lo._z }, sw::float3{ lo._x, hi._y, lo._z }, sw::float3{ hi._x, hi._y, lo._z } );
            const size_t     byteCount = list.size() * sizeof( float32 );
            const sw::string base64    = TestModelImporterInternal::encodeBase64( reinterpret_cast<const uint8*>( list.data() ), byteCount );
            sw::string       json      = R"({ "asset": { "version": "2.0" }, "scene": 0, "scenes": [ { "nodes": [ 0 ] } ], "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 36, "type": "VEC3", "min": [ -1, -0.5, -0.25 ], "max": [ 1, 0.5, 0.25 ] } ],
  "bufferViews": [ { "buffer": 0, "byteLength": )";
            json += std::to_string( byteCount ).c_str();
            json += R"( } ],
  "buffers": [ { "byteLength": )";
            json += std::to_string( byteCount ).c_str();
            json += R"(, "uri": "data:application/octet-stream;base64,)";
            json += base64;
            json += R"(" } ] })";
            return json;
        }
    };
} // namespace

/**
 * @brief [ModelImporterTest] glTF 를 엔진 좌표계로 읽는다 — X 를 뒤집고, 감김은 엔진 앞면(바깥 = (b-a)×(c-a))을 지키고, 노드 이동을 적용하고,
 *        노멀이 없으면 면 노멀, 색은 baseColorFactor, 선 프리미티브는 건너뛴다
 */
SW_TEST_CASE( ModelImporterTest, ReadsGltfIntoEngineSpaceWithWindingAndNodeTransform )
{
    const sw::string sourcePath = test::makeTempPath( "triangle.gltf" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelImporterInternal::makeTriangleGltf( 4.0f ) ) );

    sw::vector<sw::RHIVertex> listVertex;
    {
        test::ScopedDefensiveTestLog expected( "the LINES primitive is skipped with a warning" );
        SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel( sourcePath, sw::editor::ModelImportRule{}, listVertex ) );
    }
    SW_ASSERT_EQUAL( size_t( 6 ), listVertex.size() );

    // 노드 0: X 만 뒤집힌다. 노드 1: glTF 에서 (2,3,4) 만큼 옮긴 뒤 X 를 뒤집는다.
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.0f, 1.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 3.0f, 4.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -3.0f, 3.0f, 4.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 4.0f, 4.0f } ) );

    for ( size_t triangleStart = 0; triangleStart + 2 < listVertex.size(); triangleStart += 3 )
    {
        const sw::float3 positionA = TestModelImporterInternal::getPosition( listVertex[triangleStart] );
        const sw::float3 positionB = TestModelImporterInternal::getPosition( listVertex[triangleStart + 1] );
        const sw::float3 positionC = TestModelImporterInternal::getPosition( listVertex[triangleStart + 2] );
        // glTF 에서 +Z 를 향하던 면은 엔진에서도 +Z 를 향한다(X 반전은 Z 를 건드리지 않는다). 엔진 앞면 규약: (b-a)×(c-a) 가 바깥.
        const sw::float3 faceNormal = ( positionB - positionA ).cross( positionC - positionA );
        SW_EXPECT_TRUE_MSG( faceNormal._z > 0.0f, "winding is reversed - the triangle would be back-face culled" );
        for ( size_t corner = 0; corner < 3; ++corner )
        {
            const sw::RHIVertex& vertex = listVertex[triangleStart + corner];
            SW_EXPECT_NEAR_EQUAL( 1.0f, vertex._arrNormal[2], 1e-5f );
            SW_EXPECT_NEAR_EQUAL( 0.5f, vertex._arrColor[0], 1e-6f );
            SW_EXPECT_NEAR_EQUAL( 0.25f, vertex._arrColor[1], 1e-6f );
            SW_EXPECT_NEAR_EQUAL( 1.0f, vertex._arrColor[2], 1e-6f );
            SW_EXPECT_NEAR_EQUAL( 1.0f, vertex._arrColor[3], 1e-6f );
        }
    }
}

/**
 * @brief [ModelImporterTest] 원본 경로는 같은 도메인의 `models/` 아래 `.mesh` 로 대응한다
 */
SW_TEST_CASE( ModelImporterTest, RawPathMapsToTheModelsFolder )
{
    SW_EXPECT_STREQ( "game/x/models/props/crate.mesh", sw::editor::ModelImporter::makeImportedModelPath( "game/x/models_raw/props/crate.glb" ).c_str() );
    SW_EXPECT_TRUE( sw::editor::ModelImporter::makeImportedModelPath( "game/x/models/crate.glb" ).empty() );
    SW_EXPECT_TRUE( sw::editor::ModelImporter::makeImportedModelPath( "game/x/my_models_raw_old/crate.glb" ).empty() );
}

/**
 * @brief [ModelImporterTest] 일괄 임포트가 `.mesh` 와 스탬프를 쓰고, 원본을 고치면 CheckOnly 가 어긋남을 보고하고 임포트가 그것을 닫는다
 */
SW_TEST_CASE( ModelImporterTest, StampTracksSourceChanges )
{
    const sw::string resourceRoot = test::makeTempDirectory( "model_import_resource" );
    const sw::string sourcePath   = sw::FileUtil::joinPath( resourceRoot, "game/probe/models_raw/triangle.gltf" );
    const sw::string meshPath     = sw::FileUtil::joinPath( resourceRoot, "game/probe/models/triangle.mesh" );
    const sw::string stampPath    = sw::FileUtil::joinPath( resourceRoot, "game/probe/models_raw/import.stamp" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureParentDirectoryExists( sourcePath ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelImporterInternal::makeTriangleGltf( 4.0f ) ) );
    test::ScopedDefensiveTestLog        expected( "the LINES primitive is skipped with a warning" );
    const sw::editor::ModelImportConfig config;

    // 1) 임포트한 적이 없다 — 보고만 하고 쓰지 않는다.
    sw::editor::AssetImportSummary summary = sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::CheckOnly );
    SW_EXPECT_EQUAL( 1u, summary._sourceCount );
    SW_EXPECT_EQUAL( size_t( 1 ), summary._listProblem.size() );
    SW_EXPECT_FALSE( sw::FileUtil::exists( meshPath ) );

    // 2) 임포트한다 → `.mesh` 와 스탬프가 생기고 읽힌다.
    summary = sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::ImportStale );
    SW_EXPECT_TRUE( summary.isClean() );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );
    SW_EXPECT_TRUE( sw::FileUtil::exists( stampPath ) );
    sw::vector<sw::RHIVertex> listVertex;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::loadFromResource( meshPath, listVertex ) );
    SW_EXPECT_EQUAL( size_t( 6 ), listVertex.size() );
    SW_EXPECT_TRUE( sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::CheckOnly ).isClean() );

    // 3) 원본을 고쳤다 — 어긋남이고, 임포트가 닫는다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelImporterInternal::makeTriangleGltf( 7.0f ) ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::CheckOnly )._listProblem.size() );
    SW_EXPECT_EQUAL( 1u, sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::ImportStale )._importedCount );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::loadFromResource( meshPath, listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 3.0f, 7.0f } ) );

    // 4) `.mesh` 를 손으로 바꿨다 — 어긋남이다.
    sw::vector<uint8> meshBytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( meshPath, meshBytes ) );
    meshBytes.back() = static_cast<uint8>( meshBytes.back() ^ 0xFFu );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( meshPath, meshBytes.data(), meshBytes.size() ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::CheckOnly )._listProblem.size() );
}

/**
 * @brief [ModelImporterTest] 씬 뿌리 목록이 자식 노드를 가리키는 비표준 glTF(UniGLTF 내보내기)도 맨 위 조상부터 읽는다 — 부모 변환이 들어간다
 */
SW_TEST_CASE( ModelImporterTest, SceneRootListingAChildNodeImportsFromItsTopAncestor )
{
    // 노드 0 = 부모(이동 (0,0,5)), 노드 1 = 메시 노드(이동 (1,0,0)). 씬 뿌리는 표준을 어기고 자식 1 을 적는다(조상과 겹치는 경우도 섞는다).
    const sw::string sourcePath = test::makeTempPath( "child_scene_root.gltf" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelImporterInternal::makeSceneGltf( R"("scenes": [ { "nodes": [ 1, 0 ] } ],
  "nodes": [ { "children": [ 1 ], "translation": [ 0.0, 0.0, 5.0 ] }, { "mesh": 0, "translation": [ 1.0, 0.0, 0.0 ] } ])" ) ) );

    sw::vector<sw::RHIVertex> listVertex;
    {
        test::ScopedDefensiveTestLog expected( "the non-conforming scene root is fixed up with a warning" );
        SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel( sourcePath, sw::editor::ModelImportRule{}, listVertex ) );
    }
    // 뿌리를 조상으로 바꾸고 겹친 것을 지워 삼각형은 한 번만 들어간다. 위치 = 부모 · 자식 이동의 합, X 반전.
    SW_ASSERT_EQUAL( size_t( 3 ), listVertex.size() );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -1.0f, 0.0f, 5.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 0.0f, 5.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -1.0f, 1.0f, 5.0f } ) );
}

/**
 * @brief [ModelImporterTest] GLB 도 같다 — JSON 청크만 다시 쓰고 BIN 청크는 그대로 옮겨, 조상부터 읽은 위치가 원본 계층과 같다
 */
SW_TEST_CASE( ModelImporterTest, GlbSceneRootListingAChildNodeImports )
{
    const sw::string        sourcePath = test::makeTempPath( "child_scene_root.glb" );
    const sw::vector<uint8> glbBytes   = TestModelImporterInternal::makeChildRootGlb();
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( sourcePath, glbBytes.data(), glbBytes.size() ) );

    sw::vector<sw::RHIVertex> listVertex;
    {
        test::ScopedDefensiveTestLog expected( "the non-conforming scene root is fixed up with a warning" );
        SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel( sourcePath, sw::editor::ModelImportRule{}, listVertex ) );
    }
    SW_ASSERT_EQUAL( size_t( 3 ), listVertex.size() );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 0.0f, 1.5f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -3.0f, 0.0f, 1.5f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 1.0f, 1.5f } ) );

    // translation 규칙이 배치 오프셋을 지운다(원본 공간 (-2, 0, -1.5)).
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel(
        sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::None, sw::float3{ -2.0f, 0.0f, -1.5f } ), listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.0f, 1.0f, 0.0f } ) );
}

/**
 * @brief [ModelImporterTest] 옮기는 규칙 — none 은 그대로, xz 는 경계의 XZ 중심을 원점으로, bottom-center 는 거기에 더해 가장 낮은 Y 를 0 으로,
 *        translation 은 원본 공간에서 더한다(엔진 공간에서는 X 가 뒤집힌다)
 */
SW_TEST_CASE( ModelImporterTest, RecenterMovesTheBoundsAsTheRuleSays )
{
    const sw::string sourcePath = test::makeTempPath( "offset_triangle.gltf" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelImporterInternal::makeOffsetTriangleGltf() ) );

    sw::vector<sw::RHIVertex> listVertex;
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::None ), listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 3.0f, 4.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -3.0f, 3.0f, 4.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -2.0f, 4.0f, 4.0f } ) );

    // 경계 X [-3, -2] · Z [4, 4] 의 중심 (-2.5, 4) 이 원점으로 간다. Y 는 그대로다.
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::Xz ), listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.5f, 3.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -0.5f, 3.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.5f, 4.0f, 0.0f } ) );

    // 가장 낮은 Y(3) 도 0 으로 간다.
    SW_ASSERT_TRUE(
        sw::editor::ModelImporter::readModel( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::BottomCenter ), listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.5f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -0.5f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.5f, 1.0f, 0.0f } ) );

    // 원본 공간 (1, -3, -4) 를 더하면 노드 이동 (2, 3, 4) 가 (3, 0, 0) 이 되고, X 를 뒤집어 엔진에서 (-3, 0, 0) 부터다.
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModel(
        sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::None, sw::float3{ 1.0f, -3.0f, -4.0f } ), listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -3.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -4.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ -3.0f, 1.0f, 0.0f } ) );
}

/**
 * @brief [ModelImporterTest] 임포트 규칙은 경로로 고르고, 규칙을 바꾸면 원본 해시가 바뀌어 스탬프가 어긋남이 된다. 모르는 recenter 값 · 잘못된 translation 은
 *        설정을 거부한다
 */
SW_TEST_CASE( ModelImporterTest, ImportRuleIsChosenByPathAndFeedsTheSourceHash )
{
    sw::editor::ModelImportConfig recenterConfig;
    SW_ASSERT_TRUE( recenterConfig.loadFromJsonString(
        R"({ "rules": [ { "name": "Probe", "include_patterns": [ "*/probe/models_raw/*" ], "recenter": "xz", "translation": [ 0.5, 0, -1 ] } ] })" ) );
    const sw::editor::ModelImportRule probeRule = recenterConfig.findMatchingRule( "game/probe/models_raw/a.glb" );
    SW_EXPECT_TRUE( sw::editor::ModelRecenter::Xz == probeRule._recenter );
    SW_EXPECT_NEAR_EQUAL( 0.5f, probeRule._arrTranslation[0], 0.0f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, probeRule._arrTranslation[2], 0.0f );
    SW_EXPECT_TRUE( sw::editor::ModelRecenter::None == recenterConfig.findMatchingRule( "game/other/models_raw/a.glb" )._recenter );

    {
        test::ScopedDefensiveTestLog  expected( "an unknown recenter value rejects the whole config" );
        sw::editor::ModelImportConfig brokenConfig;
        SW_EXPECT_FALSE( brokenConfig.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "recenter": "center" } ] })" ) );
        SW_EXPECT_TRUE( brokenConfig.getRules().empty() );
        SW_EXPECT_FALSE( brokenConfig.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "translation": [ 1, 2 ] } ] })" ) );
    }

    const sw::string resourceRoot = test::makeTempDirectory( "model_import_rule_resource" );
    const sw::string sourcePath   = sw::FileUtil::joinPath( resourceRoot, "game/probe/models_raw/triangle.gltf" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureParentDirectoryExists( sourcePath ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelImporterInternal::makeOffsetTriangleGltf() ) );
    SW_EXPECT_NOT_EQUAL( sw::editor::ModelImporter::computeSourceHash( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::None ) ),
                         sw::editor::ModelImporter::computeSourceHash( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::Xz ) ) );
    SW_EXPECT_NOT_EQUAL(
        sw::editor::ModelImporter::computeSourceHash( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::None ) ),
        sw::editor::ModelImporter::computeSourceHash( sourcePath, TestModelImporterInternal::makeRule( sw::editor::ModelRecenter::None, sw::float3{ 0.0f, 0.0f, 0.001f } ) ) );

    // 규칙 없이 임포트한 것은 규칙이 생기면 어긋남이고, 다시 임포트하면 규칙대로 옮겨진 메시가 나온다.
    const sw::editor::ModelImportConfig noRuleConfig;
    SW_EXPECT_TRUE( sw::editor::ModelImporter::importAllModels( resourceRoot, noRuleConfig, sw::editor::AssetImportMode::ImportStale ).isClean() );
    SW_EXPECT_TRUE( sw::editor::ModelImporter::importAllModels( resourceRoot, noRuleConfig, sw::editor::AssetImportMode::CheckOnly ).isClean() );
    SW_EXPECT_EQUAL( size_t( 1 ),
                     sw::editor::ModelImporter::importAllModels( resourceRoot, recenterConfig, sw::editor::AssetImportMode::CheckOnly )._listProblem.size() );
    SW_EXPECT_EQUAL( 1u, sw::editor::ModelImporter::importAllModels( resourceRoot, recenterConfig, sw::editor::AssetImportMode::ImportStale )._importedCount );

    sw::vector<sw::RHIVertex> listVertex;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::loadFromResource( sw::FileUtil::joinPath( resourceRoot, "game/probe/models/triangle.mesh" ), listVertex ) );
    SW_EXPECT_TRUE( TestModelImporterInternal::containsPosition( listVertex, sw::float3{ 0.5f, 3.0f, 0.0f } ) );
}

/**
 * @brief [ModelImporterTest] 저장소의 모든 모델 원본이 커밋된 `.mesh` 와 스탬프로 맞는다(규칙 포함)
 * @details 지면 `App --import-models` 로 임포트하고 `.mesh` 와 `models_raw/import.stamp` 를 함께 커밋한다.
 */
SW_TEST_CASE( ModelImporterTest, RepositoryRawModelsMatchTheirMeshes )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::editor::ModelImportConfig config;
    SW_ASSERT_TRUE( config.loadFromFile( sw::editor::ModelImportConfig::makeDefaultConfigPath() ) );

    const sw::editor::AssetImportSummary summary =
        sw::editor::ModelImporter::importAllModels( sw::ResourceUtil::getRootFolderPath(), config, sw::editor::AssetImportMode::CheckOnly );
    sw::string problemText;
    for ( const sw::string& problem : summary._listProblem )
    {
        problemText += problem;
        problemText += "\n";
    }
    SW_EXPECT_TRUE( summary._sourceCount > 0 );
    SW_EXPECT_TRUE_MSG( summary.isClean(), problemText.c_str() );
    SW_EXPECT_EQUAL( 0u, summary._importedCount );
}

/**
 * @brief [ModelImporterTest] 스킨드 모델(KayKit 기사) — 스켈레톤 41 본 · 부모 · 부착 메시 · 클립 76 개(저장소 결과), 규칙의 클립 목록 · 곁 데이터 알림
 * @details 원본을 그대로 다시 읽어(`readModelAsset`, 규칙으로 클립 둘만) 본 · 부착 · 스킨 정점을 보고, 저장소에 커밋된 임포트 결과(스켈레톤 JSON ·
 *          클립 폴더)로 전체 수를 본다. 원본 옆 곁 데이터(`<y>.clips.json`)의 알림이 클립에 실리고, 원본에 없는 클립 이름(규칙 · 곁 데이터)은 임포트 오류다.
 */
SW_TEST_CASE( ModelImporterTest, SkinnedModelImportsSkeletonClipsAndAttachments )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string resourceRoot = sw::ResourceUtil::getRootFolderPath();
    const sw::string knightPath   = sw::FileUtil::joinPath( resourceRoot, "game/shooter3d/models_raw/kaykit/knight.glb" );

    sw::editor::ModelImportRule rule;
    rule._listClipName = { "Idle", "Walking_A" };
    sw::editor::ModelImportResult result;
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModelAsset( knightPath, rule, result ) );
    SW_EXPECT_TRUE( result._bSkinned == SW_TRUE );
    SW_EXPECT_EQUAL( 41u, result._skeleton.getBoneCount() );
    const int32 hips = result._skeleton.findBoneIndex( sw::hashed_string( "hips" ) );
    SW_ASSERT_TRUE( hips > 0 );
    SW_EXPECT_EQUAL( result._skeleton.findBoneIndex( sw::hashed_string( "root" ) ), result._skeleton.getBone( static_cast<uint32>( hips ) )._parentIndex );
    for ( uint32 boneIndex = 0; boneIndex < result._skeleton.getBoneCount(); ++boneIndex )
    {
        SW_EXPECT_TRUE( result._skeleton.getBone( boneIndex )._parentIndex < static_cast<int32>( boneIndex ) ); // 부모가 앞
    }
    SW_EXPECT_TRUE( result._mesh.hasSkin() );
    SW_EXPECT_EQUAL( 41u, result._mesh._skinBoneCount );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( result._listClip.size() ) );
    bool bSwordOnRightHand = false;
    for ( const sw::editor::ModelImportAttachment& attachment : result._listAttachment )
    {
        bSwordOnRightHand = bSwordOnRightHand || ( attachment._name == "1H_Sword" && attachment._parentBone == sw::hashed_string( "handslot.r" ) );
    }
    SW_EXPECT_TRUE( bSwordOnRightHand );
    // 클립 길이 · 압축(ACL) — 오차 1 mm 아래
    for ( const sw::editor::ModelImportClip& imported : result._listClip )
    {
        SW_EXPECT_TRUE( imported._clip.getDuration() > 0.5f );
        SW_EXPECT_TRUE( imported._stats._maxError < 0.001f );
        SW_EXPECT_EQUAL( 41u, imported._clip.getTrackCount() );
    }

    // 저장소의 임포트 결과 — 스켈레톤과 클립 76 개.
    sw::Skeleton committed;
    SW_ASSERT_TRUE( committed.loadFromResource( "game/shooter3d/models/kaykit/knight/knight.skeleton.json" ) );
    SW_EXPECT_EQUAL( 41u, committed.getBoneCount() );
    SW_EXPECT_TRUE( committed.getAttachments().empty() == false );
    sw::vector<sw::string> listClipFile;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( sw::FileUtil::joinPath( resourceRoot, "game/shooter3d/models/kaykit/knight/clips" ), ".animclip", listClipFile, false ) );
    SW_EXPECT_EQUAL( 76u, static_cast<uint32>( listClipFile.size() ) );

    // 곁 데이터의 알림 · 반복이 클립에 실린다.
    const sw::string tempRoot   = test::makeTempDirectory( "skinned_import" );
    const sw::string copiedPath = sw::FileUtil::joinPath( tempRoot, "knight.glb" );
    SW_ASSERT_TRUE( sw::FileUtil::copyFile( knightPath, copiedPath ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::editor::ModelImporter::makeClipDataPath( copiedPath ),
                                                 R"({ "clips": { "Walking_A": { "loop": false, "notifies": [ { "name": "FootL", "time": 0.25 } ], "curves": { "Speed": [ [ 0, 0 ], [ 1, 2 ] ] } } } })" ) );
    sw::editor::ModelImportRule walkRule;
    walkRule._listClipName = { "Walking_A" };
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModelAsset( copiedPath, walkRule, result ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( result._listClip.size() ) );
    const sw::AnimClip& walk = result._listClip[0]._clip;
    SW_EXPECT_FALSE( walk.isLoopingByDefault() );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( walk.getNotifyTrack().getEvents().size() ) );
    SW_EXPECT_TRUE( walk.getNotifyTrack().getEvents()[0]._name == sw::hashed_string( "FootL" ) );
    SW_EXPECT_NOT_NULL( walk.findCurve( sw::hashed_string( "Speed" ) ) );

    // 원본에 없는 이름은 오류다 — 규칙의 클립 목록도, 곁 데이터의 클립 이름도.
    test::ScopedDefensiveTestLog expected( "unknown clip names in the rule or the clip data fail the import" );
    sw::editor::ModelImportRule  missingRule;
    missingRule._listClipName = { "Moonwalk" };
    SW_EXPECT_FALSE( sw::editor::ModelImporter::readModelAsset( copiedPath, missingRule, result ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::editor::ModelImporter::makeClipDataPath( copiedPath ), R"({ "clips": { "Walking_Z": { "loop": true } } })" ) );
    SW_EXPECT_FALSE( sw::editor::ModelImporter::readModelAsset( copiedPath, walkRule, result ) );
    // 모르는 규칙 키 · 뿌리 키 · 코덱은 설정 오류다.
    sw::editor::ModelImportConfig config;
    SW_EXPECT_FALSE( config.loadFromJsonString( R"({ "rulez": [] })" ) );
    SW_EXPECT_FALSE( config.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "animation_codec": "zip" } ] })" ) );
    SW_EXPECT_FALSE( config.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "animashions": false } ] })" ) );
    SW_EXPECT_TRUE( config.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "animation_codec": "raw", "clips": [ "Idle" ] } ] })" ) );
    SW_EXPECT_EQUAL( sw::string( "raw" ), config.getRules()[0]._animationCodec );
}

/**
 * @brief [ModelImporterTest] VRM 은 머티리얼마다 구간 메시 · 툰 머티리얼을 낸다 — 삼각형이 머티리얼대로 나뉘고, 정점 색에 머티리얼 색을 굽지 않는다(머티리얼이 든다)
 * @details 삼각형 두 개를 같은 위치 접근자로 두 프리미티브(머티리얼 0 · 1)가 쓰는 VRM 0.x glTF. 머티리얼 1 은 반투명 MToon 이다.
 */
SW_TEST_CASE( ModelImporterTest, VrmSplitsMeshByToonMaterial )
{
    const float32    arrPosition[9] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    const sw::string base64         = TestModelImporterInternal::encodeBase64( reinterpret_cast<const uint8*>( arrPosition ), sizeof( arrPosition ) );
    sw::string       json           = R"({ "asset": { "version": "2.0" }, "scene": 0, "scenes": [ { "nodes": [ 0 ] } ], "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 }, { "attributes": { "POSITION": 0 }, "material": 1 }, { "attributes": { "POSITION": 0 }, "material": 1 } ] } ],
  "materials": [ { "name": "Skin", "pbrMetallicRoughness": { "baseColorFactor": [ 0.5, 0.25, 1.0, 1.0 ] } }, { "name": "Glass" } ],
  "extensionsUsed": [ "VRM" ],
  "extensions": { "VRM": { "materialProperties": [
    { "name": "Skin", "shader": "VRM/MToon", "floatProperties": { "_ShadeShift": 0.0, "_ShadeToony": 0.9 }, "vectorProperties": { "_Color": [ 0.5, 0.25, 1.0, 1.0 ] } },
    { "name": "Glass", "shader": "VRM/MToon", "floatProperties": { "_BlendMode": 2 } } ] } },
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] } ],
  "bufferViews": [ { "buffer": 0, "byteLength": 36 } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,)";
    json += base64;
    json += R"(" } ] })";
    const sw::string sourcePath = test::makeTempPath( "toon.vrm.gltf" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, json ) );

    sw::editor::ModelImportResult result;
    SW_ASSERT_TRUE( sw::editor::ModelImporter::readModelAsset( sourcePath, sw::editor::ModelImportRule{}, result ) );
    SW_ASSERT_EQUAL( size_t( 2 ), result._listSection.size() );
    SW_EXPECT_STREQ( "skin", result._listSection[0]._fileStem.c_str() );
    SW_EXPECT_STREQ( "glass", result._listSection[1]._fileStem.c_str() );
    SW_EXPECT_EQUAL( size_t( 3 ), result._listSection[0]._mesh._listVertex.size() );
    SW_EXPECT_EQUAL( size_t( 6 ), result._listSection[1]._mesh._listVertex.size() );
    SW_EXPECT_TRUE( result._listSection[1]._material._alphaMode == sw::editor::ToonAlphaMode::Transparent );
    // 본 메시는 전부(9 정점)이고, 색은 흰색이다 — baseColorFactor 는 툰 머티리얼의 baseColor 다(감마 → 선형).
    SW_EXPECT_EQUAL( size_t( 9 ), result._mesh._listVertex.size() );
    for ( const sw::RHIVertex& vertex : result._listSection[0]._mesh._listVertex )
    {
        SW_EXPECT_NEAR_EQUAL( 1.0f, vertex._arrColor[0], 1e-6f );
    }
    SW_EXPECT_NEAR_EQUAL( 0.21404f, result._listSection[0]._material._baseColor._x, 1e-4f );

    // 모르는 MToon 키가 있으면 임포트가 실패한다.
    sw::string   badJson = json;
    const size_t at      = badJson.find( "\"_BlendMode\"" );
    SW_ASSERT_TRUE( at != sw::string::npos );
    badJson.replace( at, 12, "\"_BlendModes\"" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, badJson ) );
    test::ScopedDefensiveTestLog expected( "an unknown VRM material key fails the import" );
    SW_EXPECT_FALSE( sw::editor::ModelImporter::readModelAsset( sourcePath, sw::editor::ModelImportRule{}, result ) );
}

/**
 * @brief [ModelImporterTest] 규칙에 `fracture` 가 있으면 `.mesh` 옆에 `.fracture` 를 쓰고(스탬프가 그 파일까지 본다), 규칙에서 빠지면 지운다.
 *        모르는 파쇄 키 · 패턴은 설정을 거부한다
 */
SW_TEST_CASE( ModelImporterTest, FractureRuleWritesFractureAssetBesideTheMesh )
{
    sw::editor::ModelImportConfig fractureConfig;
    SW_ASSERT_TRUE( fractureConfig.loadFromJsonString( R"({ "rules": [ { "name": "Wall", "include_patterns": [ "*/probe/models_raw/*" ],
        "fracture": { "pattern": "slices", "slices": [ 3, 1, 1 ], "slice_jitter": 0, "levels": [ 2 ], "seed": 5, "interior_color": [ 1, 0, 0, 1 ] } } ] })" ) );
    const sw::editor::ModelImportRule rule = fractureConfig.findMatchingRule( "game/probe/models_raw/box.gltf" );
    SW_EXPECT_TRUE( rule._bFracture == SW_TRUE );
    SW_EXPECT_TRUE( rule._fracture._pattern == sw::FracturePattern::Slices );
    SW_EXPECT_EQUAL( 5ull, rule._fracture._seed );
    {
        test::ScopedDefensiveTestLog  expected( "an unknown fracture key or pattern rejects the whole config" );
        sw::editor::ModelImportConfig brokenConfig;
        SW_EXPECT_FALSE( brokenConfig.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "fracture": { "piece": 4 } } ] })" ) );
        SW_EXPECT_FALSE( brokenConfig.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "fracture": { "pattern": "shatter" } } ] })" ) );
        SW_EXPECT_FALSE( brokenConfig.loadFromJsonString( R"({ "rules": [ { "include_patterns": [ "*" ], "fracture": { "slices": [ 3, 0, 1 ] } } ] })" ) );
    }

    const sw::string resourceRoot = test::makeTempDirectory( "model_import_fracture_resource" );
    const sw::string sourcePath   = sw::FileUtil::joinPath( resourceRoot, "game/probe/models_raw/box.gltf" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureParentDirectoryExists( sourcePath ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, TestModelFractureInternal::makeBoxGltf() ) );
    SW_EXPECT_EQUAL( 1u, sw::editor::ModelImporter::importAllModels( resourceRoot, fractureConfig, sw::editor::AssetImportMode::ImportStale )._importedCount );
    const sw::string  fracturePath = sw::FileUtil::joinPath( resourceRoot, "game/probe/models/box.fracture" );
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( asset.loadFromResource( fracturePath ) );
    SW_EXPECT_EQUAL( 3u, asset.getPieceCount() );
    SW_EXPECT_EQUAL( size_t( 2 ), asset._graph._listLink.size() );
    SW_EXPECT_EQUAL( 5ull, asset._seed );
    SW_EXPECT_TRUE( asset.countTriangles( sw::FractureSurfaceSlot::Interior ) > 0 );
    SW_EXPECT_TRUE( sw::editor::ModelImporter::importAllModels( resourceRoot, fractureConfig, sw::editor::AssetImportMode::CheckOnly ).isClean() );

    // `.fracture` 를 손으로 지우면 어긋남이다.
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( fracturePath ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sw::editor::ModelImporter::importAllModels( resourceRoot, fractureConfig, sw::editor::AssetImportMode::CheckOnly )._listProblem.size() );
    SW_EXPECT_EQUAL( 1u, sw::editor::ModelImporter::importAllModels( resourceRoot, fractureConfig, sw::editor::AssetImportMode::ImportStale )._importedCount );
    SW_EXPECT_TRUE( sw::FileUtil::exists( fracturePath ) );

    // 규칙에서 파쇄를 빼면 다시 임포트가 옛 `.fracture` 를 지운다.
    const sw::editor::ModelImportConfig noRuleConfig;
    SW_EXPECT_EQUAL( 1u, sw::editor::ModelImporter::importAllModels( resourceRoot, noRuleConfig, sw::editor::AssetImportMode::ImportStale )._importedCount );
    SW_EXPECT_FALSE( sw::FileUtil::exists( fracturePath ) );
}
