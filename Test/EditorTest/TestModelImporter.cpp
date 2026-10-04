#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImporter.h"

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
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( meshPath ) );

    // 2) 임포트한다 → `.mesh` 와 스탬프가 생기고 읽힌다.
    summary = sw::editor::ModelImporter::importAllModels( resourceRoot, config, sw::editor::AssetImportMode::ImportStale );
    SW_EXPECT_TRUE( summary.isClean() );
    SW_EXPECT_EQUAL( 1u, summary._importedCount );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( stampPath ) );
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
