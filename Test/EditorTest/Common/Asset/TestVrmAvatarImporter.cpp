#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmAvatarImporter.h"

#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "TestFramework/TestFramework.h"

// VrmAvatarImporterTest — VRM 아바타 정보(humanoid · 표정 · 스프링 본 · 1인칭)가 엔진 이름(본 · 모프 타깃)과 엔진 공간으로 옮겨지는지.

namespace
{
    struct TestVrmAvatarImporterInternal
    {
        /** @brief 노드 0 은 얼굴 메시(타깃 셋), 1 hips, 2 엄지(0.x Proximal), 3 spine(충돌체), 4 → 5 머리카락 사슬, 6 head 입니다. */
        static sw::editor::VrmGltfNames makeNames()
        {
            sw::editor::VrmGltfNames names;
            names._listNodeName      = { "Face", "Hips", "Thumb1", "Spine", "Hair1", "Hair2", "Head" };
            names._listNodeMesh      = { 0, -1, -1, -1, -1, -1, -1 };
            names._listNodeChild     = { {}, { 3 }, {}, { 6 }, { 5 }, {}, { 4 } };
            names._listMeshName      = { "Face.baked" };
            names._listMeshMorphName = {
                { "target0", "target1", "target2" }
            };
            return names;
        }

        /** @brief VRoid 0.x 모양의 `extensions.VRM`(AvatarSample_D 의 키를 줄인 것)입니다. */
        static constexpr const utf8* kVrm0 = R"({
  "exporterVersion": "UniVRM-0.53.0",
  "meta": { "title": "Sample" },
  "humanoid": { "humanBones": [ { "bone": "hips", "node": 1, "useDefaultValues": true }, { "bone": "leftThumbProximal", "node": 2 },
                                 { "bone": "head", "node": 6 } ], "armStretch": 0.05, "hasTranslationDoF": false },
  "firstPerson": { "firstPersonBone": 6, "firstPersonBoneOffset": { "x": 0.01, "y": 0.06, "z": 0.02 },
                   "meshAnnotations": [ { "mesh": 0, "firstPersonFlag": "ThirdPersonOnly" } ], "lookAtTypeName": "Bone" },
  "blendShapeMaster": { "blendShapeGroups": [
    { "name": "Joy", "presetName": "joy", "binds": [ { "mesh": 0, "index": 1, "weight": 50 } ], "materialValues": [] },
    { "name": "Extra", "presetName": "unknown", "binds": [ { "mesh": 0, "index": 0, "weight": 100 }, { "mesh": 0, "index": 2, "weight": 100 } ],
      "materialValues": [ { "materialName": "Face", "propertyName": "_Color", "targetValue": [ 1, 0, 0, 1 ] } ], "isBinary": true } ] },
  "secondaryAnimation": {
    "boneGroups": [ { "comment": "Hair", "stiffiness": 0.75, "gravityPower": 0.1, "gravityDir": { "x": 0.5, "y": -1, "z": 0.25 }, "dragForce": 0.05,
                      "center": -1, "hitRadius": 0.02, "bones": [ 4 ], "colliderGroups": [ 0 ] } ],
    "colliderGroups": [ { "node": 3, "colliders": [ { "offset": { "x": 0.1, "y": 0.2, "z": 0.3 }, "radius": 0.12 } ] } ] },
  "materialProperties": [] })";

        /** @brief VRM 1.0 의 `VRMC_vrm` 입니다. */
        static constexpr const utf8* kVrm1 = R"({
  "specVersion": "1.0",
  "humanoid": { "humanBones": { "hips": { "node": 1 }, "leftThumbMetacarpal": { "node": 2 }, "head": { "node": 6 } } },
  "firstPerson": { "meshAnnotations": [ { "node": 0, "type": "thirdPersonOnly" } ] },
  "lookAt": { "offsetFromHeadBone": [ 0.01, 0.06, 0.02 ], "type": "bone" },
  "expressions": {
    "preset": { "happy": { "morphTargetBinds": [ { "node": 0, "index": 1, "weight": 0.8 } ], "overrideBlink": "block", "isBinary": false } },
    "custom": { "wink": { "morphTargetBinds": [ { "node": 0, "index": 2, "weight": 1.0 } ], "overrideMouth": "blend" } } } })";

        /** @brief VRM 1.0 의 `VRMC_springBone` 입니다(구 하나 · 캡슐 하나). */
        static constexpr const utf8* kSpring1 = R"({
  "specVersion": "1.0",
  "colliders": [ { "node": 3, "shape": { "sphere": { "offset": [ 0.1, 0.2, 0.3 ], "radius": 0.12 } } },
                 { "node": 6, "shape": { "capsule": { "offset": [ 0, 0, 0 ], "radius": 0.05, "tail": [ 0.2, 0, 0 ] } } } ],
  "colliderGroups": [ { "name": "Body", "colliders": [ 0, 1 ] } ],
  "springs": [ { "name": "Hair", "center": 6, "colliderGroups": [ 0 ],
                 "joints": [ { "node": 4, "hitRadius": 0.02, "stiffness": 0.5, "gravityPower": 0.1, "gravityDir": [ 0.5, -1, 0.25 ], "dragForce": 0.4 },
                             { "node": 5 } ] } ] })";

        static sw::JSONValue parse( sw::JSONDocument& document, const utf8* pText )
        {
            SW_EXPECT_TRUE( document.parse( pText ) );
            return document.getRoot();
        }

        static bool containsText( const sw::vector<sw::string>& listText, const utf8* pText )
        {
            return std::find( listText.begin(), listText.end(), sw::string( pText ) ) != listText.end();
        }

        static void expectVector( const sw::float3& expected, const sw::float3& actual )
        {
            SW_EXPECT_NEAR_EQUAL( expected._x, actual._x, 1e-6f );
            SW_EXPECT_NEAR_EQUAL( expected._y, actual._y, 1e-6f );
            SW_EXPECT_NEAR_EQUAL( expected._z, actual._z, 1e-6f );
        }
    };
} // namespace

/**
 * @brief [VrmAvatarImporterTest] VRM 0.x — 본 · 모프 타깃이 엔진 이름으로 풀리고, 이름은 1.0 의 것(joy → happy, 엄지 Proximal → Metacarpal)이며,
 *        벡터는 유니티 공간에서 엔진 공간(x · z 뒤집기)으로, 가중치는 0..100 → 0..1 로 옮겨진다
 */
SW_TEST_CASE( VrmAvatarImporterTest, Vrm0AvatarMapsToEngineNamesAndSpace )
{
    using Internal = TestVrmAvatarImporterInternal;
    sw::JSONDocument         document;
    sw::editor::VrmAvatarDef def;
    sw::vector<sw::string>   listIgnored;
    sw::string               error;
    SW_ASSERT_TRUE_MSG( sw::editor::VrmAvatarImporter::readVrm0Avatar( Internal::parse( document, Internal::kVrm0 ), Internal::makeNames(), def, &listIgnored, error ),
                        error.c_str() );

    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( def._sourceMajorVersion ) );
    SW_ASSERT_EQUAL( size_t( 3 ), def._listHumanBone.size() );
    SW_EXPECT_STREQ( "Hips", def.findBone( "hips" ).c_str() );
    SW_EXPECT_STREQ( "Thumb1", def.findBone( "leftThumbMetacarpal" ).c_str() );
    SW_EXPECT_TRUE( def.findBone( "leftThumbProximal" ).empty() );

    SW_ASSERT_EQUAL( size_t( 2 ), def._listExpression.size() );
    SW_EXPECT_STREQ( "happy", def._listExpression[0]._name.c_str() );
    SW_EXPECT_TRUE( def._listExpression[0]._bPreset == SW_TRUE );
    SW_ASSERT_EQUAL( size_t( 1 ), def._listExpression[0]._listBind.size() );
    SW_EXPECT_STREQ( "target1", def._listExpression[0]._listBind[0]._morphTarget.c_str() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, def._listExpression[0]._listBind[0]._weight, 1e-6f );
    SW_EXPECT_STREQ( "Extra", def._listExpression[1]._name.c_str() );
    SW_EXPECT_TRUE( def._listExpression[1]._bPreset == SW_FALSE );
    SW_EXPECT_TRUE( def._listExpression[1]._bBinary == SW_TRUE );
    SW_EXPECT_EQUAL( size_t( 2 ), def._listExpression[1]._listBind.size() );

    // 충돌체 offset (0.1, 0.2, 0.3)(유니티) → (-0.1, 0.2, -0.3)(엔진). 사슬은 뿌리 Hair1 과 자손 Hair2 이다.
    SW_ASSERT_EQUAL( size_t( 1 ), def._listCollider.size() );
    SW_EXPECT_STREQ( "Spine", def._listCollider[0]._bone.c_str() );
    Internal::expectVector( sw::float3{ -0.1f, 0.2f, -0.3f }, def._listCollider[0]._offset );
    SW_EXPECT_NEAR_EQUAL( 0.12f, def._listCollider[0]._radius, 1e-6f );
    SW_ASSERT_EQUAL( size_t( 1 ), def._listSpring.size() );
    const sw::editor::VrmSpring& spring = def._listSpring[0];
    SW_EXPECT_STREQ( "Hair", spring._name.c_str() );
    SW_EXPECT_TRUE( spring._centerBone.empty() );
    SW_ASSERT_EQUAL( size_t( 2 ), spring._listJoint.size() );
    SW_EXPECT_STREQ( "Hair1", spring._listJoint[0]._bone.c_str() );
    SW_EXPECT_STREQ( "Hair2", spring._listJoint[1]._bone.c_str() );
    SW_EXPECT_NEAR_EQUAL( 0.75f, spring._listJoint[1]._stiffness, 1e-6f );
    Internal::expectVector( sw::float3{ -0.5f, -1.0f, -0.25f }, spring._listJoint[0]._gravityDirection );
    SW_ASSERT_EQUAL( size_t( 1 ), spring._listColliderGroup.size() );

    SW_EXPECT_STREQ( "Head", def._firstPersonBone.c_str() );
    Internal::expectVector( sw::float3{ -0.01f, 0.06f, -0.02f }, def._firstPersonOffset );
    SW_ASSERT_EQUAL( size_t( 1 ), def._listFirstPersonMesh.size() );
    SW_EXPECT_STREQ( "Face.baked", def._listFirstPersonMesh[0]._mesh.c_str() );
    SW_EXPECT_TRUE( def._listFirstPersonMesh[0]._mode == sw::editor::VrmFirstPersonMode::ThirdPersonOnly );

    // 옮기지 않는 값(시선 · 머티리얼 표정)은 무시 목록에 남는다. 유니티 근육 값(armStretch)과 meta 는 조용히 넘긴다.
    SW_EXPECT_TRUE( Internal::containsText( listIgnored, "VRM.firstPerson.lookAt*" ) );
    SW_EXPECT_TRUE( Internal::containsText( listIgnored, "VRM.blendShapeMaster.blendShapeGroups.materialValues" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), listIgnored.size() );
}

/**
 * @brief [VrmAvatarImporterTest] VRM 1.0 — humanoid 맵 · 프리셋과 사용자 표정(노드 → 메시 → 모프 타깃) · override · 구와 캡슐 충돌체 · 관절마다의 값 · 시선 오프셋
 */
SW_TEST_CASE( VrmAvatarImporterTest, Vrm1AvatarReadsExpressionsAndSpringBones )
{
    using Internal = TestVrmAvatarImporterInternal;
    sw::JSONDocument         document;
    sw::JSONDocument         springDocument;
    sw::editor::VrmAvatarDef def;
    sw::vector<sw::string>   listIgnored;
    sw::string               error;
    SW_ASSERT_TRUE_MSG( sw::editor::VrmAvatarImporter::readVrm1Avatar( Internal::parse( document, Internal::kVrm1 ), Internal::parse( springDocument, Internal::kSpring1 ),
                                                                       Internal::makeNames(), def, &listIgnored, error ),
                        error.c_str() );

    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( def._sourceMajorVersion ) );
    SW_EXPECT_STREQ( "Thumb1", def.findBone( "leftThumbMetacarpal" ).c_str() );
    SW_ASSERT_EQUAL( size_t( 2 ), def._listExpression.size() );
    SW_EXPECT_STREQ( "happy", def._listExpression[0]._name.c_str() );
    SW_EXPECT_TRUE( def._listExpression[0]._overrideBlink == sw::editor::VrmExpressionOverride::Block );
    SW_EXPECT_STREQ( "target1", def._listExpression[0]._listBind[0]._morphTarget.c_str() );
    SW_EXPECT_NEAR_EQUAL( 0.8f, def._listExpression[0]._listBind[0]._weight, 1e-6f );
    SW_EXPECT_STREQ( "wink", def._listExpression[1]._name.c_str() );
    SW_EXPECT_TRUE( def._listExpression[1]._bPreset == SW_FALSE );
    SW_EXPECT_TRUE( def._listExpression[1]._overrideMouth == sw::editor::VrmExpressionOverride::Blend );

    // 1.0 은 glTF 공간이라 X 만 뒤집는다.
    SW_ASSERT_EQUAL( size_t( 2 ), def._listCollider.size() );
    Internal::expectVector( sw::float3{ -0.1f, 0.2f, 0.3f }, def._listCollider[0]._offset );
    SW_EXPECT_TRUE( def._listCollider[0]._bCapsule == SW_FALSE );
    SW_EXPECT_TRUE( def._listCollider[1]._bCapsule == SW_TRUE );
    Internal::expectVector( sw::float3{ -0.2f, 0.0f, 0.0f }, def._listCollider[1]._tail );
    SW_ASSERT_EQUAL( size_t( 1 ), def._listColliderGroup.size() );
    SW_EXPECT_EQUAL( size_t( 2 ), def._listColliderGroup[0]._listCollider.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), def._listSpring.size() );
    SW_EXPECT_STREQ( "Head", def._listSpring[0]._centerBone.c_str() );
    SW_ASSERT_EQUAL( size_t( 2 ), def._listSpring[0]._listJoint.size() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, def._listSpring[0]._listJoint[0]._stiffness, 1e-6f );
    Internal::expectVector( sw::float3{ -0.5f, -1.0f, 0.25f }, def._listSpring[0]._listJoint[0]._gravityDirection );
    // 값이 없는 관절은 1.0 기본값이다.
    SW_EXPECT_NEAR_EQUAL( 1.0f, def._listSpring[0]._listJoint[1]._stiffness, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, def._listSpring[0]._listJoint[1]._dragForce, 1e-6f );

    SW_EXPECT_STREQ( "Head", def._firstPersonBone.c_str() );
    Internal::expectVector( sw::float3{ -0.01f, 0.06f, 0.02f }, def._firstPersonOffset );
    SW_ASSERT_EQUAL( size_t( 1 ), def._listFirstPersonMesh.size() );
    SW_EXPECT_TRUE( def._listFirstPersonMesh[0]._mode == sw::editor::VrmFirstPersonMode::ThirdPersonOnly );
    SW_EXPECT_TRUE( Internal::containsText( listIgnored, "VRMC_vrm.lookAt" ) );
}

/**
 * @brief [VrmAvatarImporterTest] 없는 노드 · 모프 타깃 · 모르는 humanoid 본 · 표정 프리셋은 오류다 · 모르는 키는 무시 목록에 남는다
 */
SW_TEST_CASE( VrmAvatarImporterTest, BadReferencesFailAndUnknownKeysAreListed )
{
    using Internal                       = TestVrmAvatarImporterInternal;
    const sw::editor::VrmGltfNames names = Internal::makeNames();
    sw::editor::VrmAvatarDef       def;
    sw::vector<sw::string>         listIgnored;
    sw::string                     error;

    sw::JSONDocument badNode;
    SW_EXPECT_FALSE( sw::editor::VrmAvatarImporter::readVrm0Avatar( Internal::parse( badNode, R"({ "humanoid": { "humanBones": [ { "bone": "hips", "node": 40 } ] } })" ),
                                                                    names, def, &listIgnored, error ) );
    SW_EXPECT_TRUE( error.find( "node 40" ) != sw::string::npos );

    sw::JSONDocument badBone;
    SW_EXPECT_FALSE( sw::editor::VrmAvatarImporter::readVrm0Avatar( Internal::parse( badBone, R"({ "humanoid": { "humanBones": [ { "bone": "tail", "node": 1 } ] } })" ),
                                                                    names, def, &listIgnored, error ) );

    sw::JSONDocument badMorph;
    SW_EXPECT_FALSE( sw::editor::VrmAvatarImporter::readVrm0Avatar(
        Internal::parse( badMorph, R"({ "blendShapeMaster": { "blendShapeGroups": [ { "name": "A", "presetName": "a", "binds": [ { "mesh": 0, "index": 9, "weight": 100 } ] } ] } })" ),
        names, def, &listIgnored, error ) );
    SW_EXPECT_TRUE( error.find( "morph target 9" ) != sw::string::npos );

    sw::JSONDocument badPreset;
    SW_EXPECT_FALSE( sw::editor::VrmAvatarImporter::readVrm0Avatar(
        Internal::parse( badPreset, R"({ "blendShapeMaster": { "blendShapeGroups": [ { "name": "X", "presetName": "grin", "binds": [] } ] } })" ), names, def, &listIgnored,
        error ) );

    sw::JSONDocument unknownKey;
    listIgnored.clear();
    SW_EXPECT_TRUE( sw::editor::VrmAvatarImporter::readVrm1Avatar(
        Internal::parse( unknownKey, R"({ "humanoid": { "humanBones": { "hips": { "node": 1 } } }, "expresions": {} })" ), sw::JSONValue{}, names, def, &listIgnored,
        error ) );
    SW_EXPECT_TRUE( Internal::containsText( listIgnored, "VRMC_vrm.expresions (unknown key)" ) );
}

/**
 * @brief [VrmAvatarImporterTest] `.avatar.xml` — humanoid 는 맵, 나머지는 배열이고 실수는 1 μm 로 반올림한다(0.06 이 0.0599999… 가 되지 않는다)
 */
SW_TEST_CASE( VrmAvatarImporterTest, AvatarXMLIsReadable )
{
    using Internal = TestVrmAvatarImporterInternal;
    sw::JSONDocument         document;
    sw::editor::VrmAvatarDef def;
    sw::string               error;
    SW_ASSERT_TRUE( sw::editor::VrmAvatarImporter::readVrm0Avatar( Internal::parse( document, Internal::kVrm0 ), Internal::makeNames(), def, nullptr, error ) );

    const sw::string text = sw::editor::VrmAvatarImporter::makeAvatarXML( def );
    SW_EXPECT_TRUE( text == sw::editor::VrmAvatarImporter::makeAvatarXML( def ) );
    sw::XMLDocument written;
    SW_ASSERT_TRUE( written.parse( text, "avatar" ) );
    const sw::XMLNode root = written.getRoot( "VrmAvatar", false );
    SW_ASSERT_TRUE( root.isValid() );
    SW_EXPECT_EQUAL( sw::editor::VrmAvatarImporter::kVersion, root.readRequiredAttribute<uint32>( "_version" ).value() );
    SW_EXPECT_STREQ( "Hips", sw::string( root.findChild( "_mapHumanBone", false ).getAttributeText( "hips", false ) ).c_str() );
    SW_EXPECT_STREQ( "Head", sw::string( root.getAttributeText( "_firstPersonBone", false ) ).c_str() );
    SW_EXPECT_STREQ( "-0.01,0.06,-0.02", sw::string( root.getAttributeText( "_firstPersonOffset", false ) ).c_str() );
    const sw::XMLNode expression = root.findChild( "_listExpression", false ).findChild();
    SW_EXPECT_STREQ( "happy", sw::string( expression.getAttributeText( "_name", false ) ).c_str() );
    SW_EXPECT_STREQ( "target1", sw::string( expression.findChild( "_listBind", false ).findChild().getAttributeText( "_morphTarget", false ) ).c_str() );
    size_t jointCount{ 0 };
    for ( sw::XMLNode joint = root.findChild( "_listSpring", false ).findChild().findChild( "_listJoint", false ).findChild(); joint.isValid(); joint = joint.findNextSibling() )
    {
        ++jointCount;
    }
    SW_EXPECT_EQUAL( size_t( 2 ), jointCount );
    SW_EXPECT_STREQ( "ThirdPersonOnly", sw::string( root.findChild( "_listFirstPersonMesh", false ).findChild().getAttributeText( "_mode", false ) ).c_str() );
}

/**
 * @brief [VrmAvatarImporterTest] 저장소의 VRoid 0.x 원본(AvatarSample_D) — humanoid 54 본 · 표정 15 · 스프링 사슬 22(관절 60) · 충돌체 28 이 모두 풀린다
 * @details GLB 의 JSON 청크만 읽어 이름 표를 만든다(cgltf 없이 — 노드 이름과 메시의 타깃 수). 모프 타깃 이름은 메시 extras 에 없으므로 "target<번호>" 다
 *          (UniVRM 은 targetNames 를 프리미티브 extras 에 쓴다 — 임포터의 `makeMorphTargetName` 과 같은 결과다).
 */
SW_TEST_CASE( VrmAvatarImporterTest, RepositoryVroidSampleResolves )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string  path = sw::FileUtil::joinPath( sw::ResourcePaths::getRootFolderPath(), "game/empty/models_raw/avatarsample_d.vrm" );
    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( path, bytes ) );
    SW_ASSERT_TRUE( bytes.size() > 20 );
    uint32 jsonLength{ 0 };
    std::memcpy( &jsonLength, bytes.data() + 12, sizeof( jsonLength ) );
    SW_ASSERT_TRUE( 20u + jsonLength <= bytes.size() );
    sw::JSONDocument document;
    SW_ASSERT_TRUE( document.parse( sw::string_view( reinterpret_cast<const utf8*>( bytes.data() + 20 ), jsonLength ) ) );
    const sw::JSONValue root = document.getRoot();

    sw::editor::VrmGltfNames names;
    const sw::JSONValue      nodes = root.get( "nodes", false );
    for ( size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex )
    {
        const sw::JSONValue node = nodes.at( nodeIndex );
        names._listNodeName.push_back( node.get( "name", false ).asString() );
        const std::optional<int32> mesh = node.get( "mesh", false ).read<int32>();
        names._listNodeMesh.push_back( mesh.has_value() ? *mesh : -1 ); // -1 = 메시 없는 노드(`VrmGltfNames::_listNodeMesh`)
        sw::vector<uint32>  listChild;
        const sw::JSONValue children = node.get( "children", false );
        for ( size_t childIndex = 0; childIndex < children.size(); ++childIndex )
        {
            listChild.push_back( static_cast<uint32>( children.at( childIndex ).read<uint64>().value() ) );
        }
        names._listNodeChild.push_back( std::move( listChild ) );
    }
    const sw::JSONValue meshes = root.get( "meshes", false );
    for ( size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex )
    {
        const sw::JSONValue mesh = meshes.at( meshIndex );
        names._listMeshName.push_back( mesh.get( "name", false ).asString() );
        const size_t           targetCount = mesh.get( "primitives", false ).at( 0 ).get( "targets", false ).size();
        sw::vector<sw::string> listMorph;
        for ( size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex )
        {
            listMorph.push_back( "target" + sw::to_string( static_cast<uint64>( targetIndex ) ) );
        }
        names._listMeshMorphName.push_back( std::move( listMorph ) );
    }

    sw::editor::VrmAvatarDef def;
    sw::vector<sw::string>   listIgnored;
    sw::string               error;
    SW_ASSERT_TRUE_MSG( sw::editor::VrmAvatarImporter::readVrm0Avatar( root.get( "extensions", false ).get( "VRM", false ), names, def, &listIgnored, error ),
                        error.c_str() );
    SW_EXPECT_EQUAL( size_t( 54 ), def._listHumanBone.size() );
    SW_EXPECT_STREQ( "J_Bip_C_Hips", def.findBone( "hips" ).c_str() );
    SW_EXPECT_STREQ( "J_Bip_L_Thumb1", def.findBone( "leftThumbMetacarpal" ).c_str() );
    SW_EXPECT_STREQ( "J_Bip_L_Thumb2", def.findBone( "leftThumbProximal" ).c_str() );
    SW_EXPECT_EQUAL( size_t( 15 ), def._listExpression.size() );
    SW_EXPECT_EQUAL( size_t( 22 ), def._listSpring.size() );
    size_t jointCount{ 0 };
    for ( const sw::editor::VrmSpring& spring : def._listSpring )
    {
        jointCount += spring._listJoint.size();
    }
    SW_EXPECT_EQUAL( size_t( 60 ), jointCount );
    SW_EXPECT_STREQ( "Bust_0", def._listSpring[0]._name.c_str() );
    SW_EXPECT_STREQ( "J_Sec_L_Bust1", def._listSpring[0]._listJoint[0]._bone.c_str() );
    SW_EXPECT_EQUAL( size_t( 28 ), def._listCollider.size() );
    SW_EXPECT_STREQ( "J_Bip_C_Head", def._firstPersonBone.c_str() );
    bool bHasHappy = false;
    for ( const sw::editor::VrmExpression& expression : def._listExpression )
    {
        bHasHappy = bHasHappy || ( expression._name == "happy" && expression._listBind.size() == 1 && expression._listBind[0]._morphTarget == "target2" );
    }
    SW_EXPECT_TRUE( bHasHappy );
}
