// ModelImporter — 머티리얼이 여럿인 일반 glTF: 텍스처 머티리얼이 있으면 머티리얼마다 구간 + PBR 머티리얼, 텍스처 없는 머티리얼만이면 색을 구운 메시 하나.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/ModelImport/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImport/ModelImportMaterial.h"
#include "Editor/Common/Asset/ModelImport/ModelImporter.h"
#include "Editor/Common/Asset/Tests/EditorScratchDomain.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /**
     * @brief 삼각형 둘(프리미티브마다 하나, 머티리얼 0 · 1)인 glTF 를 `<root>/models_raw/pair.gltf` 에 씁니다(바깥 `.bin`, 기본색 이미지는 `textures_raw/`).
     * @param bTextured true 면 머티리얼 0 이 기본색 텍스처를 가진다.
     */
    bool writeTwoMaterialGltf( const string& root, bool bTextured, string& outSourcePath )
    {
        const string modelFolder   = FileUtil::joinPath( root, "models_raw" );
        const string textureFolder = FileUtil::joinPath( root, "textures_raw/pair" );
        if ( FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( modelFolder ) ) == false || FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( textureFolder ) ) == false )
            return false;
        if ( FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( textureFolder, "wood.png" ) ), "wood" ) == false )
            return false;
        // 위치 6 개(삼각형 둘) — 바닥면의 두 삼각형이다.
        const float32 arrPosition[18]{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 1 };
        if ( FileUtil::writeFile( FileUtil::toDiskPath( FileUtil::joinPath( modelFolder, "pair.bin" ) ), reinterpret_cast<const uint8*>( arrPosition ), sizeof( arrPosition ) ) == false )
            return false;
        const string                              texture = bTextured ? R"(, "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 }, "metallicFactor": 0 })" : "";
        StringBuilder<constant::buffer::kMax2048> json;
        json.appendFormat( R"({ "asset": { "version": "2.0" }, "scene": 0, "scenes": [ { "nodes": [ 0 ] } ], "nodes": [ { "mesh": 0 } ],
            "buffers": [ { "uri": "pair.bin", "byteLength": 72 } ],
            "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 }, { "buffer": 0, "byteOffset": 36, "byteLength": 36 } ],
            "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 0, 1 ] },
                           { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 0, 1 ] } ],
            "images": [ { "uri": "../textures_raw/pair/wood.png", "mimeType": "image/png" } ],
            "textures": [ { "source": 0 } ],
            "materials": [ { "name": "Wood", "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true%# }, { "name": "Stone", "pbrMetallicRoughness": { "baseColorFactor": [ 0.5, 0.5, 0.5, 1 ] } } ],
            "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 }, { "attributes": { "POSITION": 1 }, "material": 1 } ] } ] })",
                           texture );
        outSourcePath = FileUtil::joinPath( modelFolder, "pair.gltf" );
        return FileUtil::writeTextFile( FileUtil::toDiskPath( outSourcePath ), json.view() );
    }
} // namespace

/**
 * @brief [ModelImportSectionsTest] 텍스처 머티리얼이 있고 머티리얼이 둘이면 머티리얼마다 구간이 되고 PBR 머티리얼 표가 glTF 순서로 찬다
 * @details 첫 머티리얼만 쓰던 때는 돌 면이 나무 텍스처로 그려졌다(Quaternius 메가키트의 절반이 머티리얼 둘 이상이다).
 */
SW_TEST_CASE( ModelImportSectionsTest, TexturedMultiMaterialGltfSplitsIntoSections )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportSections" );
    string             sourcePath;
    SW_REQUIRE_TRUE( writeTwoMaterialGltf( root.str(), true, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImporter::readModelAsset( sourcePath, ModelImportRule{}, result ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, result._listPBRMaterial.size() );
    SW_EXPECT_EQUAL( string( "wood" ), result._listPBRMaterial[0]._name );
    SW_EXPECT_EQUAL( string( "wood_albedo" ), result._listPBRMaterial[0]._albedo._fileStem );
    SW_EXPECT_TRUE( result._listPBRMaterial[0]._bAlphaCutoff == SW_TRUE ); // alphaMode MASK — 잎 · 꽃이 네모로 그려지지 않는다
    SW_EXPECT_NEAR( 0.3f, result._listPBRMaterial[0]._alphaCutoff, 1e-6f );
    SW_EXPECT_TRUE( result._listPBRMaterial[0]._bTwoSided == SW_TRUE );
    SW_EXPECT_TRUE( result._listPBRMaterial[1]._bAlphaCutoff == SW_FALSE );
    SW_EXPECT_NEAR( 0.5f, result._listPBRMaterial[1]._baseColor._x, 1e-6f );
    SW_EXPECT_TRUE( result._pbrMaterial._bPresent == SW_FALSE ); // 구간 없는 메시의 머티리얼은 쓰지 않는다
    SW_REQUIRE_EQUAL( size_t{ 2 }, result._listSection.size() );
    SW_EXPECT_EQUAL( string( "wood" ), result._listSection[0]._fileStem );
    SW_EXPECT_EQUAL( string( "stone" ), result._listSection[1]._fileStem );
    SW_EXPECT_EQUAL( uint32{ 3 }, result._listSection[1]._firstIndex );
    SW_EXPECT_EQUAL( uint32{ 3 }, result._listSection[1]._indexCount );
    SW_EXPECT_EQUAL( size_t{ 2 }, result._mesh._listSection.size() );
    // 구간은 원본의 날 머티리얼 이름을 든다(파일 이름 꼴 "wood" 가 아니라 "Wood") — 다시 임포트할 때 이름을 지키는 열쇠다.
    SW_EXPECT_EQUAL( string( "Wood" ), result._listSection[0]._sourceMaterial );
    SW_EXPECT_TRUE( result._mesh._listSection[1]._sourceMaterial == hashed_string( "Stone" ) );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ModelImportSectionsTest] 관절 사이의 관절 아닌 노드 변환이 자식 본의 자리에 합쳐진다(glTF 는 관절 사이에 관절 아닌 노드를 허락한다)
 */
SW_TEST_CASE( ModelImportSectionsTest, NonJointNodeBetweenJointsKeepsTheBonePlace )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportJointOffset" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root.join( "models_raw" ) ) );
    // 위치 3 개(36 바이트) · 관절 번호 3 개(u8x4, 12 바이트) · 가중치 3 개(f32x4, 48 바이트).
    vector<uint8> listByte( 96, 0 );
    const float32 arrPosition[9]{ 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const float32 arrWeight[12]{ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    Memory::copy( listByte.data(), arrPosition, sizeof( arrPosition ) );
    listByte[36 + 8] = 1; // 셋째 정점은 둘째 관절
    Memory::copy( listByte.data() + 48, arrWeight, sizeof( arrWeight ) );
    SW_REQUIRE_TRUE( FileUtil::writeFile( root.join( "models_raw/rig.bin" ), listByte.data(), listByte.size() ) );
    const string_view json       = R"({ "asset": { "version": "2.0" }, "scene": 0, "scenes": [ { "nodes": [ 0, 3 ] } ],
        "nodes": [ { "name": "Hips", "children": [ 1 ] }, { "name": "Offset", "translation": [ 0, 2, 0 ], "children": [ 2 ] }, { "name": "Spine", "translation": [ 0, 1, 0 ] },
                   { "name": "Body", "mesh": 0, "skin": 0 } ],
        "skins": [ { "joints": [ 0, 2 ] } ],
        "buffers": [ { "uri": "rig.bin", "byteLength": 96 } ],
        "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 }, { "buffer": 0, "byteOffset": 36, "byteLength": 12 }, { "buffer": 0, "byteOffset": 48, "byteLength": 48 } ],
        "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 0, 1 ] },
                       { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" }, { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" } ],
        "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 } } ] } ] })";
    const string      sourcePath = root.join( "models_raw/rig.gltf" ).str();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( sourcePath ), json ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImporter::readModelAsset( sourcePath, ModelImportRule{}, result ) );
    const optional<uint32> spine = result._skeleton.findBoneIndex( hashed_string( "Spine" ) );
    SW_REQUIRE_TRUE( spine.has_value() );
    SW_EXPECT_EQUAL( 0, result._skeleton.getBone( *spine )._parentIndex );
    SW_EXPECT_NEAR( 3.0f, result._skeleton.getBone( *spine )._localTransform._translation._y, 1e-5f );
}

/**
 * @brief [ModelImportSectionsTest] 장비 메시의 원본 해시는 몸 메시의 내용을 따른다 — 몸을 다시 임포트하면 장비도 어긋남이다(스탬프 · DDC 열쇠)
 */
SW_TEST_CASE( ModelImportSectionsTest, SourceHashFollowsTheBodyMesh )
{
    const test::EditorScratchDomain domain;
    string                          sourcePath;
    SW_REQUIRE_TRUE( writeTwoMaterialGltf( domain.makeDiskPath( "garment" ).str(), false, sourcePath ) );
    ModelImportRule rule{};
    rule._bodySurface = test::EditorScratchDomain::makeID( "body.mesh" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "body.mesh" ), "body one" ) );
    const uint64 before = ModelImporter::computeSourceHash( sourcePath, rule, {} );
    SW_EXPECT_TRUE( before != 0 );
    SW_EXPECT_EQUAL( before, ModelImporter::computeSourceHash( sourcePath, rule, {} ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "body.mesh" ), "body two" ) );
    SW_EXPECT_TRUE( before != ModelImporter::computeSourceHash( sourcePath, rule, {} ) );
}

/**
 * @brief [ModelImportSectionsTest] 텍스처 없는 머티리얼만 여럿이면 구간 없이 색을 정점에 구운 메시 하나다(로우폴리 팩의 색 머티리얼)
 */
SW_TEST_CASE( ModelImportSectionsTest, UntexturedMultiMaterialGltfStaysOneMesh )
{
    const AbsolutePath root = test::makeScratchPath( "ModelImportSectionsFlat" );
    string             sourcePath;
    SW_REQUIRE_TRUE( writeTwoMaterialGltf( root.str(), false, sourcePath ) );

    ModelImportResult result;
    SW_REQUIRE_TRUE( ModelImporter::readModelAsset( sourcePath, ModelImportRule{}, result ) );
    SW_EXPECT_TRUE( result._listPBRMaterial.empty() );
    SW_EXPECT_TRUE( result._listSection.empty() );
    SW_EXPECT_TRUE( result._mesh._listSection.empty() );
    SW_EXPECT_EQUAL( size_t{ 6 }, result._mesh._listVertex.size() );

    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
