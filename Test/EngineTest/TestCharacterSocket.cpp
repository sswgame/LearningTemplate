#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Character/BodyShape.h"
#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Character/ReferencePoseOverride.h"
#include "Engine/Character/ResolvedSocketTable.h"
#include "Engine/Character/SocketImportUtil.h"
#include "Engine/Character/SocketSet.h"

#include "EngineTest/CharacterTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// 소켓 에셋 · 층 덮어쓰기 · 해석된 소켓 표(이름 공간 · 안정 번호 · 후보) · 레퍼런스 포즈 덮어쓰기 · 소켓 초안 · 체형(축 · 본 비율).

namespace
{
    struct CharacterSocketTestInternal
    {
        static constexpr const utf8* kKindsXml = "<SocketKinds><Kind name='Attach'/><Kind name='GroundPoint'/><Kind name='HitboxCenter'/><Kind name='LockOn'/></SocketKinds>";

        static SocketKindTable makeKinds()
        {
            SocketKindTable kinds;
            (void)kinds.loadFromXmlText( kKindsXml, "test.socketkinds.xml" );
            return kinds;
        }

        static SocketSet loadSockets( const utf8* pXmlText, const CharacterBoneArray* pBones = nullptr )
        {
            SocketSet sockets;
            (void)sockets.loadFromXmlText( pXmlText, "test.sockets.xml", makeKinds(), pBones );
            return sockets;
        }

        /** @brief 본 하나(뿌리)짜리 강체 부품의 본 배열 — 본 없는 부품의 암묵 스켈레톤. */
        static CharacterBoneArray makeRootOnlyBones()
        {
            CharacterBoneArray bones;
            (void)bones.addBone( hashed_string( "root" ), -1, float4x4::Identity );
            return bones;
        }

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return float3::getDistance( expected, actual ) <= tolerance; }
    };
} // namespace

/**
 * @brief [SocketSetTest] 소켓 · 가상 본을 읽고 본 변환에서 유닛 공간 변환을 계산한다
 */
SW_TEST_CASE( SocketSetTest, LoadsSocketsAndComputesTransforms )
{
    using Internal                 = CharacterSocketTestInternal;
    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    SocketSet                sockets;
    SW_ASSERT_TRUE( sockets.loadFromXmlText( "<SocketSet>"
                                             "  <VirtualBone name='AimRef' from='upperarm' to='hand' weight='0.5'/>"
                                             "  <Socket name='Wrist' parent='hand' kind='Attach' translation='0 0.1 0' rotation='0 90 0' preview='engine/models/cube.mesh'/>"
                                             "  <Socket name='Aim' parent='AimRef' kind='LockOn'/>"
                                             "  <Socket name='Feet' kind='GroundPoint' translation='0 -0.1 0'/>"
                                             "</SocketSet>",
                                             "arm.sockets.xml", Internal::makeKinds(), &bones ) );
    SW_ASSERT_EQUAL( size_t( 3 ), sockets.getSockets().size() );
    const SocketDef* pWrist = sockets.findSocket( hashed_string( "Wrist" ) );
    SW_ASSERT_NOT_NULL( pWrist );
    SW_EXPECT_STREQ( "engine/models/cube.mesh", pWrist->_previewMesh.c_str() );
    SW_EXPECT_TRUE( pWrist->_kind == hashed_string( "Attach" ) );

    float4x4 transform;
    SW_ASSERT_TRUE( sockets.computeSocketTransform( *pWrist, bones, transform ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, 1.1f, 0.0f ), transform.getTranslation(), 1.0e-5f ) );
    // 요 90° — 소켓의 앞(+Z)이 +X 를 본다.
    SW_EXPECT_TRUE( Internal::isNear( float3::UnitX, float3::transformVector( float3::UnitZ, transform ), 1.0e-4f ) );
    SW_ASSERT_TRUE( sockets.computeSocketTransform( *sockets.findSocket( hashed_string( "Aim" ) ), bones, transform ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, 0.5f, 0.0f ), transform.getTranslation(), 1.0e-5f ) ); // 두 본 사이 반
    SW_ASSERT_TRUE( sockets.computeSocketTransform( *sockets.findSocket( hashed_string( "Feet" ) ), bones, transform ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, -0.1f, 0.0f ), transform.getTranslation(), 1.0e-5f ) ); // 부모가 비면 뿌리
}

/**
 * @brief [SocketSetTest] 모르는 종류 · 모르는 부모 본 · 모르는 속성 · 겹친 이름은 로드 오류
 */
SW_TEST_CASE( SocketSetTest, UnknownNamesAreLoadErrors )
{
    using Internal                 = CharacterSocketTestInternal;
    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    const SocketKindTable    kinds = Internal::makeKinds();
    test::ScopedLogCollector logs;
    SW_TEST_DEFENSIVE_SCOPE( "unknown socket kinds, bones and attributes are load errors" );
    SocketSet sockets;
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><Socket name='A' kind='Holster'/></SocketSet>", "a.sockets.xml", kinds ) );
    SW_EXPECT_TRUE( logs.countContaining( "unknown kind 'Holster'" ) > 0 );
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><Socket name='A' parent='tail'/></SocketSet>", "a.sockets.xml", kinds, &bones ) );
    SW_EXPECT_TRUE( logs.countContaining( "unknown parent bone 'tail'" ) > 0 );
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><Socket name='A' offset='0 0 1'/></SocketSet>", "a.sockets.xml", kinds ) );
    SW_EXPECT_TRUE( logs.countContaining( "unknown attribute 'offset'" ) > 0 );
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><Socket name='A'/><Socket name='A'/></SocketSet>", "a.sockets.xml", kinds ) );
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><VirtualBone name='V' from='hand' to='toe'/></SocketSet>", "a.sockets.xml", kinds, &bones ) );
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><Socket name='A' anchor='Skin'/></SocketSet>", "a.sockets.xml", kinds ) );
    SW_EXPECT_FALSE( sockets.loadFromXmlText( "<SocketSet><Socket name='A' translation='0 1'/></SocketSet>", "a.sockets.xml", kinds ) );
}

/**
 * @brief [SocketSetTest] 층 덮어쓰기 — 스켈레톤 → 메시 → 외형, 위층은 적은 칸만 바꾸고 새 이름은 더한다
 */
SW_TEST_CASE( SocketSetTest, LayersOverrideOnlyWrittenFieldsByName )
{
    using Internal            = CharacterSocketTestInternal;
    SocketSet       layered   = Internal::loadSockets( "<SocketSet><Socket name='Back' parent='upperarm' kind='Attach' translation='0 0.3 -0.1'/></SocketSet>" );
    const SocketSet meshLayer = Internal::loadSockets( "<SocketSet><Socket name='Back' translation='0 0.3 -0.2'/></SocketSet>" );
    const SocketSet lookLayer = Internal::loadSockets( "<SocketSet><Socket name='Back' rotation='0 180 0'/><Socket name='Hip' parent='root'/></SocketSet>" );
    layered.applyOverride( meshLayer );
    layered.applyOverride( lookLayer );
    const SocketDef* pBack = layered.findSocket( hashed_string( "Back" ) );
    SW_ASSERT_NOT_NULL( pBack );
    SW_EXPECT_TRUE( pBack->_parent == hashed_string( "upperarm" ) );                                                           // 스켈레톤 몫
    SW_EXPECT_TRUE( pBack->_kind == hashed_string( "Attach" ) );                                                               // 스켈레톤 몫
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, 0.3f, -0.2f ), pBack->_translation, 1.0e-6f ) );                           // 메시 몫
    SW_EXPECT_NEAR_EQUAL( 180.0f, MathUtil::abs( pBack->_rotation.getEulerAngles()._y * MathUtil::RadianToDegree ), 1.0e-2f ); // 외형 몫
    SW_EXPECT_NOT_NULL( layered.findSocket( hashed_string( "Hip" ) ) );

    // 쓰고 다시 읽어도 같은 것 — 적은 칸만 쓴다.
    SocketSet reloaded;
    SW_ASSERT_TRUE( reloaded.loadFromXmlText( layered.saveToXmlText(), "saved.sockets.xml", Internal::makeKinds() ) );
    const SocketDef* pReloaded = reloaded.findSocket( hashed_string( "Back" ) );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_EXPECT_TRUE( pReloaded->_parent == hashed_string( "upperarm" ) );
    SW_EXPECT_TRUE( Internal::isNear( pBack->_translation, pReloaded->_translation, 1.0e-6f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( pBack->_rotation.dot( pReloaded->_rotation ) ), 1.0e-5f );
}

/**
 * @brief [ResolvedSocketTableTest] 몸 + 부품 소켓을 슬롯 접두어로 한 이름 공간에 — 무기를 바꿔도 번호가 같고 새 무기의 총구를 가리킨다
 */
SW_TEST_CASE( ResolvedSocketTableTest, SlotPrefixedNamesStayStableAcrossResolve )
{
    using Internal                     = CharacterSocketTestInternal;
    const CharacterBoneArray bodyBones = test::CharacterTestUtil::makeArmBones();
    const CharacterBoneArray gunBones  = Internal::makeRootOnlyBones();
    const SocketSet          body      = Internal::loadSockets( "<SocketSet><Socket name='Hand' parent='hand'/></SocketSet>" );
    const SocketSet          rifle     = Internal::loadSockets( "<SocketSet><Socket name='Muzzle' translation='0 0 0.5'/></SocketSet>" );
    const SocketSet          shotgun   = Internal::loadSockets( "<SocketSet><Socket name='Muzzle' translation='0 0 0.8'/><Socket name='Ejector' translation='0 0.1 0.2'/></SocketSet>" );

    const CharacterBoneArray* arrBones[2] = { &bodyBones, &gunBones };
    const float4x4            arrWorld[2] = { float4x4::createTranslation( 10.0f, 0.0f, 0.0f ), float4x4::createTranslation( 1.0f, 0.0f, 0.0f ) };
    SocketPoseView            pose;
    pose._listUnitBones = vector_reference<const CharacterBoneArray* const>( arrBones, 2 );
    pose._listUnitWorld = vector_reference<const float4x4>( arrWorld, 2 );

    ResolvedSocketTable table;
    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, body, bodyBones, nullptr, nullptr ) );
    SW_ASSERT_TRUE( table.addUnit( hashed_string( "MainHand" ), 1, rifle, gunBones, nullptr, nullptr ) );
    table.endResolve();
    const SocketId muzzleId = table.findSocket( hashed_string( "MainHand.Muzzle" ) );
    SW_ASSERT_TRUE( muzzleId != kInvalidSocketId );
    SW_EXPECT_TRUE( table.findSocket( hashed_string( "Muzzle" ) ) == kInvalidSocketId ); // 부품 것은 접두어가 붙는다
    float4x4 world;
    SW_ASSERT_TRUE( table.getSocketTransform( hashed_string( "MainHand.Muzzle" ), pose, world ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 1.0f, 0.0f, 0.5f ), world.getTranslation(), 1.0e-5f ) );
    SW_ASSERT_TRUE( table.getSocketTransform( hashed_string( "Hand" ), pose, world ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 10.0f, 1.0f, 0.0f ), world.getTranslation(), 1.0e-5f ) ); // 몸 것은 접두어 없음

    // 무기를 바꿔 다시 해석 — 같은 이름, 같은 번호, 새 총구.
    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, body, bodyBones, nullptr, nullptr ) );
    SW_ASSERT_TRUE( table.addUnit( hashed_string( "MainHand" ), 1, shotgun, gunBones, nullptr, nullptr ) );
    table.endResolve();
    SW_EXPECT_EQUAL( muzzleId, table.findSocket( hashed_string( "MainHand.Muzzle" ) ) );
    SW_ASSERT_TRUE( table.getSocketTransform( muzzleId, pose, world ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 1.0f, 0.0f, 0.8f ), world.getTranslation(), 1.0e-5f ) );
    SW_EXPECT_TRUE( table.isSocketActive( table.findSocket( hashed_string( "MainHand.Ejector" ) ) ) );

    // 무기를 내려놓음 — 번호는 남고 꺼진다.
    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, body, bodyBones, nullptr, nullptr ) );
    table.endResolve();
    SW_EXPECT_EQUAL( muzzleId, table.findSocket( hashed_string( "MainHand.Muzzle" ) ) );
    SW_EXPECT_FALSE( table.isSocketActive( muzzleId ) );
    SW_EXPECT_FALSE( table.getSocketTransform( muzzleId, pose, world ) );

    // 부모 본을 대조한다 — 없는 본이면 그 유닛은 들어가지 않는다.
    string error;
    table.beginResolve();
    SW_EXPECT_FALSE( table.addUnit( hashed_string( "OffHand" ), 1, Internal::loadSockets( "<SocketSet><Socket name='Grip' parent='hand'/></SocketSet>" ), gunBones,
                                    nullptr, &error ) );
    SW_EXPECT_TRUE( StringUtil::contains( error, "unknown parent bone 'hand'" ) );
}

/**
 * @brief [ResolvedSocketTableTest] 후보 목록 — 벨트가 있으면 벨트 걸이로, 없으면 자기 자리(몸의 허리)로
 */
SW_TEST_CASE( ResolvedSocketTableTest, FallbackCandidatesPickFirstPresentSocket )
{
    using Internal                        = CharacterSocketTestInternal;
    const CharacterBoneArray  bodyBones   = test::CharacterTestUtil::makeArmBones();
    const CharacterBoneArray  beltBones   = Internal::makeRootOnlyBones();
    const SocketSet           body        = Internal::loadSockets( "<SocketSet>"
                                                                                    "  <Socket name='Waist' parent='root' translation='0 0.9 0'/>"
                                                                                    "  <Socket name='Scabbard' parent='root' translation='0 0.85 -0.1' fallback='Belt.Hook'/>"
                                                                                    "</SocketSet>" );
    const SocketSet           belt        = Internal::loadSockets( "<SocketSet><Socket name='Hook' translation='0.2 0.9 0'/></SocketSet>" );
    const CharacterBoneArray* arrBones[2] = { &bodyBones, &beltBones };
    SocketPoseView            pose;
    pose._listUnitBones = vector_reference<const CharacterBoneArray* const>( arrBones, 2 );

    ResolvedSocketTable table;
    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, body, bodyBones, nullptr, nullptr ) );
    SW_ASSERT_TRUE( table.addUnit( hashed_string( "Belt" ), 1, belt, beltBones, nullptr, nullptr ) );
    table.endResolve();
    const SocketId scabbardId = table.findSocket( hashed_string( "Scabbard" ) );
    SW_EXPECT_EQUAL( table.findSocket( hashed_string( "Belt.Hook" ) ), table.resolveTarget( scabbardId ) );
    float4x4 world;
    SW_ASSERT_TRUE( table.getSocketTransform( scabbardId, pose, world ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.2f, 0.9f, 0.0f ), world.getTranslation(), 1.0e-5f ) );

    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, body, bodyBones, nullptr, nullptr ) );
    table.endResolve();
    SW_EXPECT_EQUAL( scabbardId, table.resolveTarget( scabbardId ) );
    SW_ASSERT_TRUE( table.getSocketTransform( scabbardId, pose, world ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, 0.85f, -0.1f ), world.getTranslation(), 1.0e-5f ) );

    const hashed_string arrCandidate[2] = { hashed_string( "Belt.Hook" ), hashed_string( "Waist" ) };
    SW_EXPECT_EQUAL( table.findSocket( hashed_string( "Waist" ) ), table.findFirstActiveSocket( vector_reference<const hashed_string>( arrCandidate, 2 ) ) );
}

/**
 * @brief [ResolvedSocketTableTest] 체형 보정 — 본 기준 소켓은 본 비율을, 표면 기준 소켓은 체형 모프의 표면을 따른다
 */
SW_TEST_CASE( ResolvedSocketTableTest, SocketsFollowBodyShape )
{
    using Internal                                  = CharacterSocketTestInternal;
    const CharacterBoneArray              bindBones = test::CharacterTestUtil::makeArmBones();
    test::CharacterTestUtil::CylinderDesc desc;
    desc._radius            = 0.04f;
    AppearanceGeometry body = test::CharacterTestUtil::makeCylinder( desc );
    test::CharacterTestUtil::skinBySplit( body, 0.5f, 1, 2 );
    GeometryMorphTarget belly;
    belly._name = hashed_string( "Belly" );
    for ( uint32 vertex = 0; vertex < body.getVertexCount(); ++vertex )
    {
        const float3& position = body._listPosition[vertex];
        const float32 weight   = CharacterGeometryUtil::computeFalloff( MathUtil::abs( position._y - 0.25f ) / 0.2f );
        belly._listPositionDelta.push_back( float3( position._x, 0.0f, position._z ) * ( 0.5f * weight ) ); // 반지름 × 1.5
    }
    body._listMorph.push_back( belly );
    const SocketSet     sockets = Internal::loadSockets( "<SocketSet>"
                                                             "  <Socket name='BellyFront' parent='upperarm' anchor='Surface' translation='0 0.25 0.04'/>"
                                                             "  <Socket name='Wrist' parent='hand' translation='0 0.05 0'/>"
                                                             "</SocketSet>" );
    ResolvedSocketTable table;
    table.beginResolve();
    SW_ASSERT_TRUE( table.addUnit( hashed_string(), 0, sockets, bindBones, &body, nullptr ) );
    table.endResolve();

    // 체형: 배 모프 1 + 아래팔 길이 +0.1(본 비율).
    AppearanceGeometry shaped = body;
    BodyShapeUtil::applyMorphs( shaped, vector<BodyMorphWeight>{
                                            BodyMorphWeight{ hashed_string( "Belly" ), 1.0f }
    } );
    table.applyShapedGeometry( 0, shaped );
    CharacterBoneArray shapedBones = bindBones;
    BoneProportion     proportion;
    proportion.setBone( hashed_string( "hand" ), float3( 1.0f ), float3( 0.0f, 0.1f, 0.0f ) );
    proportion.apply( shapedBones );

    float4x4 transform;
    SW_ASSERT_TRUE( table.computeUnitTransform( table.findSocket( hashed_string( "BellyFront" ) ), shapedBones, transform ) );
    SW_EXPECT_NEAR_EQUAL( 0.06f, transform.getTranslation()._z, 1.0e-3f ); // 배가 나온 만큼 앞으로
    SW_ASSERT_TRUE( table.computeUnitTransform( table.findSocket( hashed_string( "Wrist" ) ), shapedBones, transform ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, 1.15f, 0.0f ), transform.getTranslation(), 1.0e-5f ) ); // 본 비율을 따라
}

/**
 * @brief [ReferencePoseOverrideTest] 적은 칸만 덮어쓰고, 좌우 대칭 짝으로 뒤집어 적고, 모르는 본은 로드 오류
 */
SW_TEST_CASE( ReferencePoseOverrideTest, OverridesWrittenFieldsAndMirrorsPairs )
{
    CharacterBoneArray bones;
    const int32        root = bones.addBone( hashed_string( "root" ), -1, float4x4::createTranslation( 0.0f, 1.0f, 0.0f ) );
    (void)bones.addBone( hashed_string( "arm_l" ), root, float4x4::createTranslation( -0.2f, 0.0f, 0.0f ) );
    (void)bones.addBone( hashed_string( "arm_r" ), root, float4x4::createTranslation( 0.2f, 0.0f, 0.0f ) );

    ReferencePoseOverride pose;
    SW_ASSERT_TRUE( pose.loadFromXmlText( "<ReferencePose mirrorAxis='X'>"
                                          "  <Bone name='arm_l' translation='-0.25 0 0' rotation='0 0 -40'/>"
                                          "  <Bone name='root' scale='1.1 1.1 1.1'/>"
                                          "  <Mirror left='arm_l' right='arm_r'/>"
                                          "</ReferencePose>",
                                          "body.refpose.xml", &bones ) );
    SW_ASSERT_TRUE( pose.mirrorOverride( hashed_string( "arm_l" ) ) );
    const BoneOverride* pRight = pose.findOverride( hashed_string( "arm_r" ) );
    SW_ASSERT_NOT_NULL( pRight );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pRight->_translation._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 40.0f, pRight->_rotation.getEulerAngles()._z * MathUtil::RadianToDegree, 1.0e-3f );

    CharacterBoneArray applied = bones;
    pose.apply( applied );
    SW_EXPECT_NEAR_EQUAL( 1.0f, applied._listLocal[0].getTranslation()._y, 1.0e-6f ); // 스케일만 적은 뿌리는 위치를 지킨다
    SW_EXPECT_NEAR_EQUAL( 1.1f, applied._listLocal[0].getScale()._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f * 1.1f, applied._listModel[2].getTranslation()._x, 1.0e-5f ); // 뿌리 스케일이 자식 자리를 옮긴다

    ReferencePoseOverride reloaded;
    SW_ASSERT_TRUE( reloaded.loadFromXmlText( pose.saveToXmlText(), "saved.refpose.xml", &bones ) );
    SW_EXPECT_TRUE( reloaded.findMirrorBone( hashed_string( "arm_r" ) ) == hashed_string( "arm_l" ) );
    SW_EXPECT_NOT_NULL( reloaded.findOverride( hashed_string( "arm_r" ) ) );

    test::ScopedLogCollector logs;
    SW_TEST_DEFENSIVE_SCOPE( "unknown bones in a reference pose override are load errors" );
    ReferencePoseOverride broken;
    SW_EXPECT_FALSE( broken.loadFromXmlText( "<ReferencePose><Bone name='tail' scale='2 2 2'/></ReferencePose>", "broken.refpose.xml", &bones ) );
    SW_EXPECT_TRUE( logs.countContaining( "unknown bone 'tail'" ) > 0 );
}

/**
 * @brief [SocketImportTest] 임포트 노드(본 + 로컬 변환)로 소켓 초안을 만들고, 파일이 있으면 다시 쓰지 않는다
 */
SW_TEST_CASE( SocketImportTest, CreatesDraftAndNeverOverwritesHumanFile )
{
    using Internal = CharacterSocketTestInternal;
    SocketImportNode node;
    node._name           = hashed_string( "Muzzle" );
    node._parentBone     = hashed_string( "barrel" );
    node._kind           = hashed_string( "Attach" );
    node._localTransform = float4x4::createTranslation( 0.0f, 0.0f, 0.42f );
    SocketSet draft;
    SocketImportUtil::createSocketSet( vector_reference<const SocketImportNode>( &node, 1 ), draft );
    SW_ASSERT_NOT_NULL( draft.findSocket( hashed_string( "Muzzle" ) ) );
    SW_EXPECT_NEAR_EQUAL( 0.42f, draft.findSocket( hashed_string( "Muzzle" ) )->_translation._z, 1.0e-6f );

    const string path     = test::makeTempPath( "rifle.sockets.xml" );
    bool         bWritten = false;
    SW_ASSERT_TRUE( SocketImportUtil::writeIfMissing( path, draft, bWritten ) );
    SW_EXPECT_TRUE( bWritten );

    // 사람이 고친 뒤 다시 임포트 — 파일은 그대로다.
    node._localTransform = float4x4::createTranslation( 0.0f, 0.0f, 0.9f );
    SocketImportUtil::createSocketSet( vector_reference<const SocketImportNode>( &node, 1 ), draft );
    SW_ASSERT_TRUE( SocketImportUtil::writeIfMissing( path, draft, bWritten ) );
    SW_EXPECT_FALSE( bWritten );
    SocketSet onDisk;
    SW_ASSERT_TRUE( onDisk.loadFromResource( path, Internal::makeKinds() ) );
    SW_EXPECT_NEAR_EQUAL( 0.42f, onDisk.findSocket( hashed_string( "Muzzle" ) )->_translation._z, 1.0e-6f );
}

/**
 * @brief [BodyShapeTest] 축 값 → 모프 가중치 · 본 비율, 범위로 묶기, 모르는 축은 오류
 */
SW_TEST_CASE( BodyShapeTest, AxesResolveToMorphWeightsAndBoneProportion )
{
    BodyShapeSet shapes;
    SW_ASSERT_TRUE( shapes.loadFromXmlText( "<BodyShapeSet>"
                                            "  <Axis name='Weight' min='-1' max='1'>"
                                            "    <Positive morph='Heavy'><Bone name='upperarm' scale='1.2 1 1.2'/></Positive>"
                                            "    <Negative morph='Thin'/>"
                                            "  </Axis>"
                                            "  <Axis name='Height' min='-1' max='2'>"
                                            "    <Positive><Bone name='lowerarm' offset='0 0.1 0'/></Positive>"
                                            "  </Axis>"
                                            "</BodyShapeSet>",
                                            "human.bodyshape.xml" ) );
    vector<BodyMorphWeight> listMorphWeight;
    BoneProportion          proportion;
    vector<BodyShapeValue>  listValue{
        BodyShapeValue{hashed_string( "Weight" ), 0.5f},
        BodyShapeValue{hashed_string( "Height" ), 5.0f}
    };
    SW_ASSERT_TRUE( shapes.evaluate( listValue, listMorphWeight, proportion, nullptr ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listMorphWeight.size() );
    SW_EXPECT_TRUE( listMorphWeight[0]._morph == hashed_string( "Heavy" ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, listMorphWeight[0]._weight, 1.0e-6f );
    SW_ASSERT_NOT_NULL( proportion.findBone( hashed_string( "upperarm" ) ) );
    SW_EXPECT_NEAR_EQUAL( 1.1f, proportion.findBone( hashed_string( "upperarm" ) )->_scale._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, proportion.findBone( hashed_string( "lowerarm" ) )->_offset._y, 1.0e-6f ); // 2 로 묶여 끝(1)

    listValue = {
        BodyShapeValue{ hashed_string( "Weight" ), -1.0f }
    };
    SW_ASSERT_TRUE( shapes.evaluate( listValue, listMorphWeight, proportion, nullptr ) );
    SW_ASSERT_EQUAL( size_t( 1 ), listMorphWeight.size() );
    SW_EXPECT_TRUE( listMorphWeight[0]._morph == hashed_string( "Thin" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, listMorphWeight[0]._weight, 1.0e-6f );

    string error;
    listValue = {
        BodyShapeValue{ hashed_string( "Muscle" ), 1.0f }
    };
    SW_EXPECT_FALSE( shapes.evaluate( listValue, listMorphWeight, proportion, &error ) );
    SW_EXPECT_TRUE( StringUtil::contains( error, "unknown body shape axis 'Muscle'" ) );

    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    AppearanceGeometry       body;
    SW_EXPECT_FALSE( shapes.validate( &body, &bones, &error ) ); // 몸에 Heavy · Thin 모프가 없다
}

/**
 * @brief [BodyShapeTest] 본 비율은 애니메이션 위의 가산 층 — 회전은 애니메이션 것, 위치 · 스케일은 보정이 더해진다. 형상 스키닝이 따른다
 */
SW_TEST_CASE( BodyShapeTest, BoneProportionLayersOverAnimationAndSkinsGeometry )
{
    const CharacterBoneArray bindBones = test::CharacterTestUtil::makeArmBones();
    CharacterBoneArray       animated  = bindBones;
    // 애니메이션: 아래팔을 Z 축으로 90° 굽힘.
    animated._listLocal[2] = float4x4::createRotationZ( MathUtil::HalfPi ) * float4x4::createTranslation( 0.0f, 0.5f, 0.0f );
    animated.computeModelTransforms();
    BoneProportion proportion;
    proportion.setBone( hashed_string( "hand" ), float3( 1.0f ), float3( 0.0f, 0.1f, 0.0f ) ); // 아래팔 길이 +0.1
    proportion.apply( animated );
    // 손은 굽힌 아래팔 축(-X)으로 0.6 떨어진다.
    SW_EXPECT_NEAR_EQUAL( -0.6f, animated._listModel[3].getTranslation()._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, animated._listModel[3].getTranslation()._y, 1.0e-5f );

    test::CharacterTestUtil::CylinderDesc desc;
    desc._yMin              = 1.0f;
    desc._yMax              = 1.2f;
    desc._ringCount         = 4;
    AppearanceGeometry hand = test::CharacterTestUtil::makeCylinder( desc );
    test::CharacterTestUtil::skinToBone( hand, 3 );
    AppearanceGeometry posed;
    BodyShapeUtil::skinToPose( hand, bindBones, animated, posed );
    // 손 정점(바인드 y=1) → 손 본 자리.
    for ( uint32 vertex = 0; vertex < hand.getVertexCount(); ++vertex )
    {
        if ( MathUtil::abs( hand._listPosition[vertex]._y - 1.0f ) > 1.0e-5f )
            continue;
        SW_EXPECT_NEAR_EQUAL( -0.6f, posed._listPosition[vertex]._x, 0.06f );
        SW_EXPECT_NEAR_EQUAL( 0.5f, posed._listPosition[vertex]._y, 0.06f );
    }
}
