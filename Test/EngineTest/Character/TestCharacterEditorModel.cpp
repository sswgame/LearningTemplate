#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/Socket/SocketSet.h"

#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterMirrorUtil.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/EditSnapshotHistory.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/SocketEditModel.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 캐릭터 편집 창의 편집 모델 — 좌우 이름 규칙 · 위치/회전 뒤집기, 소켓 편집(저장 필드 비트, 이름 바꾸기, 대칭, 검증, 파일 왕복), 되돌리기 기록, 외형 선택 편집.

namespace
{
    struct CharacterEditorModelTestInternal
    {
        static constexpr const utf8* kKindsXML = "<SocketKinds><Kind name='Attach'/><Kind name='GroundPoint'/></SocketKinds>";
        static constexpr const utf8* kMirrorXML =
            "<MirrorNames><Pair first='_L' second='_R'/><Pair first='.L' second='.R'/><Pair first='_l' second='_r'/><Pair first='.l' second='.r'/>"
            "<Pair first='Left' second='Right'/><Pair first='left' second='right'/><Pair first='L_' second='R_'/><Pair first='l_' second='r_'/></MirrorNames>";

        static MirrorNameRules makeRules()
        {
            MirrorNameRules rules;
            const bool      bLoaded = rules.loadFromXMLText( kMirrorXML, "test.mirrornames.xml" );
            SW_ASSERT( bLoaded );
            (void)bLoaded;
            return rules;
        }

        static SocketKindTable makeKinds()
        {
            SocketKindTable kinds;
            (void)kinds.loadFromXMLText( kKindsXML, "test.socketkinds.xml" ); // 테스트 준비 — 결과는 각 테스트가 상태로 확인한다
            return kinds;
        }

        static SocketEditModel makeModel( const utf8* pXMLText )
        {
            SocketEditModel model;
            model.setKindTable( makeKinds() );
            model.setMirrorRules( makeRules() );
            if ( pXMLText != nullptr )
            {
                const bool bLoaded = model.loadFromXMLText( pXMLText, "test.sockets.xml" );
                SW_ASSERT( bLoaded );
                (void)bLoaded;
            }
            return model;
        }

        static string mirrorOf( const MirrorNameRules& rules, string_view name )
        {
            string mirrored;
            return rules.findMirrorName( name, mirrored ) ? mirrored : string( "<none>" );
        }

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return float3::getDistance( expected, actual ) <= tolerance; }
    };
} // namespace

/**
 * @brief [MirrorNameRulesTest] 좌우 토큰은 단어 경계에서만 짝으로 보고, 가운데 이름은 짝이 없다
 */
SW_TEST_CASE( MirrorNameRulesTest, FindsCounterpartOnlyAtTokenBoundaries )
{
    using Internal = CharacterEditorModelTestInternal;

    const MirrorNameRules rules = Internal::makeRules();
    SW_EXPECT_STREQ( "hand_R", Internal::mirrorOf( rules, "hand_L" ).c_str() );
    SW_EXPECT_STREQ( "hand_L", Internal::mirrorOf( rules, "hand_R" ).c_str() );
    SW_EXPECT_STREQ( "upperarm.r", Internal::mirrorOf( rules, "upperarm.l" ).c_str() );
    SW_EXPECT_STREQ( "RightHand", Internal::mirrorOf( rules, "LeftHand" ).c_str() );
    SW_EXPECT_STREQ( "ArmLeft", Internal::mirrorOf( rules, "ArmRight" ).c_str() );
    SW_EXPECT_STREQ( "R_Hand", Internal::mirrorOf( rules, "L_Hand" ).c_str() );
    SW_EXPECT_STREQ( "arm_R_01", Internal::mirrorOf( rules, "arm_L_01" ).c_str() );

    // 단어 경계가 아닌 곳의 같은 철자는 짝이 아니다.
    SW_EXPECT_STREQ( "<none>", Internal::mirrorOf( rules, "Shoulder_Pad" ).c_str() );
    SW_EXPECT_STREQ( "<none>", Internal::mirrorOf( rules, "Leftover" ).c_str() );
    SW_EXPECT_STREQ( "<none>", Internal::mirrorOf( rules, "spine_01" ).c_str() );
    SW_EXPECT_TRUE( rules.isCenterName( "pelvis" ) );
    SW_EXPECT_FALSE( rules.isCenterName( "foot_L" ) );

    MirrorNameRules custom;
    SW_EXPECT_FALSE( custom.addPair( "", "_R" ) );
    SW_EXPECT_FALSE( custom.addPair( "_L", "_L" ) );
    SW_ASSERT_TRUE( custom.addPair( "_Lt", "_Rt" ) );
    SW_EXPECT_STREQ( "eye_Rt", Internal::mirrorOf( custom, "eye_Lt" ).c_str() );
}

/**
 * @brief [MirrorNameRulesTest] 이름 표는 XML 로 왕복하고, 잘못된 쌍은 표를 바꾸지 않은 채 실패한다
 */
SW_TEST_CASE( MirrorNameRulesTest, RoundTripsThroughXMLAndRejectsBadPairs )
{
    using Internal = CharacterEditorModelTestInternal;

    MirrorNameRules rules = Internal::makeRules();
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( rules.getPairs().size() ) );

    MirrorNameRules reloaded;
    SW_ASSERT_TRUE( reloaded.loadFromXMLText( rules.saveToXMLText(), "roundtrip.mirrornames.xml" ) );
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( reloaded.getPairs().size() ) );
    SW_EXPECT_STREQ( "hand_R", Internal::mirrorOf( reloaded, "hand_L" ).c_str() );

    // 같은 두 토큰 · 모르는 원소 · 모르는 속성은 오류이고, 이전 표는 그대로다.
    SW_EXPECT_FALSE( rules.loadFromXMLText( "<MirrorNames><Pair first='_L' second='_L'/></MirrorNames>", "bad1.mirrornames.xml" ) );
    SW_EXPECT_FALSE( rules.loadFromXMLText( "<MirrorNames><Twin first='_L' second='_R'/></MirrorNames>", "bad2.mirrornames.xml" ) );
    SW_EXPECT_FALSE( rules.loadFromXMLText( "<MirrorNames><Pair first='_L' second='_R' weight='1'/></MirrorNames>", "bad3.mirrornames.xml" ) );
    SW_EXPECT_EQUAL( 8u, static_cast<uint32>( rules.getPairs().size() ) );
}

/**
 * @brief [CharacterMirrorUtilTest] 위치는 법선 성분만 뒤집히고, 뒤집힌 회전은 뒤집힌 벡터에 대해 같은 결과를 만든다
 */
SW_TEST_CASE( CharacterMirrorUtilTest, MirrorsTranslationAndConjugatesRotation )
{
    using Internal = CharacterEditorModelTestInternal;

    const float3 mirrored = CharacterMirrorUtil::mirrorTranslation( float3( 1.0f, 2.0f, 3.0f ), MirrorAxis::X );
    SW_EXPECT_TRUE( Internal::isNear( float3( -1.0f, 2.0f, 3.0f ), mirrored, 1.0e-6f ) );

    SocketDef original;
    original._rotation         = quaternion::makeFromYawPitchRoll( float3( 0.3f, 0.7f, -0.4f ) );
    SocketDef flipped          = original;
    flipped._rotation          = CharacterMirrorUtil::mirrorRotation( original._rotation, MirrorAxis::X );
    const float3 probe         = float3( 0.2f, 0.9f, -0.5f );
    const float3 rotatedProbe  = float3::transformVector( probe, original.makeLocalTransform() );
    const float3 expected      = CharacterMirrorUtil::mirrorTranslation( rotatedProbe, MirrorAxis::X );
    const float3 mirroredProbe = CharacterMirrorUtil::mirrorTranslation( probe, MirrorAxis::X );
    const float3 actual        = float3::transformVector( mirroredProbe, flipped.makeLocalTransform() );
    SW_EXPECT_TRUE( Internal::isNear( expected, actual, 1.0e-4f ) );
}

/**
 * @brief [EditSnapshotHistoryTest] 같은 키의 연속 편집은 한 단계가 되고, 새 편집은 다시 하기를 버린다
 */
SW_TEST_CASE( EditSnapshotHistoryTest, CoalescesSameKeyAndDropsRedoOnNewEdit )
{
    EditSnapshotHistory<int32> history;
    int32                      value = 0;

    history.record( value, "drag" );
    value = 1;
    history.record( value, "drag" );
    value = 2;
    history.record( value, "drag" );
    value = 3;
    SW_EXPECT_EQUAL( size_t( 1 ), history.getUndoCount() ); // 끌기 한 번

    history.endCoalescing();
    history.record( value, "drag" );
    value = 4;
    SW_EXPECT_EQUAL( size_t( 2 ), history.getUndoCount() );

    SW_ASSERT_TRUE( history.undo( value ) );
    SW_EXPECT_EQUAL( 3, value );
    SW_ASSERT_TRUE( history.undo( value ) );
    SW_EXPECT_EQUAL( 0, value );
    SW_EXPECT_FALSE( history.undo( value ) );
    SW_ASSERT_TRUE( history.redo( value ) );
    SW_EXPECT_EQUAL( 3, value );

    history.record( value, {} ); // 새 편집이 다시 하기를 버린다
    SW_EXPECT_FALSE( history.canRedo() );

    for ( size_t count = 0; count < EditSnapshotHistory<int32>::kMaxCount + 10; ++count )
    {
        history.record( value, {} );
    }
    SW_EXPECT_EQUAL( EditSnapshotHistory<int32>::kMaxCount, history.getUndoCount() );
}

/**
 * @brief [SocketEditModelTest] 값을 고친 필드가 저장 텍스트에 실리고, 읽기 → 쓰기 → 읽기가 같은 값을 돌려준다
 */
SW_TEST_CASE( SocketEditModelTest, EditedFieldsAreSavedAndRoundTripThroughFile )
{
    using Internal = CharacterEditorModelTestInternal;

    SocketEditModel model = Internal::makeModel( "<SocketSet><Socket name='Muzzle' parent='barrel' kind='Attach'/></SocketSet>" );
    SW_EXPECT_FALSE( model.isDirty() );
    string error;

    // 파일에 적지 않았던 translation 은 비트를 켜지 않으면 저장되지 않는다.
    SW_ASSERT_TRUE( model.setTranslation( hashed_string( "Muzzle" ), float3( 0.0f, 0.0f, 0.42f ), false, error ) );
    SW_ASSERT_TRUE( model.setRotationDegrees( hashed_string( "Muzzle" ), float3( 0.0f, 90.0f, 0.0f ), false, error ) );
    SW_ASSERT_TRUE( model.addSocket( hashed_string( "Grip" ), hashed_string( "hand_r" ), hashed_string( "Attach" ), error ) );
    SW_ASSERT_TRUE( model.setAnchor( hashed_string( "Grip" ), SocketAnchor::Surface, error ) );
    SW_EXPECT_TRUE( model.isDirty() );

    const string path = test::makeTempPath( "edit.sockets.xml" );
    SW_ASSERT_TRUE( model.saveToFile( path ) );
    SW_EXPECT_FALSE( model.isDirty() );

    SocketEditModel reloaded = Internal::makeModel( nullptr );
    SW_ASSERT_TRUE( reloaded.loadFromFile( path ) );
    SW_ASSERT_EQUAL( size_t( 2 ), reloaded.getSockets().getSockets().size() );
    const SocketDef* pMuzzle = reloaded.findSocket( hashed_string( "Muzzle" ) );
    SW_ASSERT_NOT_NULL( pMuzzle );
    SW_EXPECT_NEAR_EQUAL( 0.42f, pMuzzle->_translation._z, 1.0e-6f );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.0f, 90.0f, 0.0f ), pMuzzle->_rotation.getEulerAngles() * MathUtil::kRadianToDegree, 1.0e-3f ) );
    SW_EXPECT_TRUE( reloaded.findSocket( hashed_string( "Grip" ) )->_anchor == SocketAnchor::Surface );
    SW_EXPECT_FALSE( reloaded.isDirty() );
}

/**
 * @brief [SocketEditModelTest] 이름 바꾸기는 순서를 유지하고 다른 소켓의 후보 목록을 따라가며, 잘못된 편집은 표도 기록도 바꾸지 않는다
 */
SW_TEST_CASE( SocketEditModelTest, RenameKeepsOrderFollowsFallbackAndRejectsBadEdits )
{
    using Internal = CharacterEditorModelTestInternal;

    SocketEditModel model = Internal::makeModel( "<SocketSet>"
                                                 "<Socket name='Hook' parent='pelvis' kind='Attach'/>"
                                                 "<Socket name='Scabbard' parent='spine' kind='Attach' fallback='Hook'/>"
                                                 "<Socket name='Last' parent='spine' kind='Attach'/>"
                                                 "</SocketSet>" );
    string          error;

    SW_EXPECT_FALSE( model.renameSocket( hashed_string( "Hook" ), hashed_string( "Last" ), error ) ); // 겹침
    SW_EXPECT_FALSE( model.renameSocket( hashed_string( "Missing" ), hashed_string( "X" ), error ) );
    SW_EXPECT_FALSE( model.addSocket( hashed_string( "Hook" ), hashed_string{}, hashed_string{}, error ) );
    SW_EXPECT_FALSE( model.addSocket( hashed_string( "Bad" ), hashed_string{}, hashed_string( "NoSuchKind" ), error ) );
    SW_EXPECT_FALSE( model.setFallback( hashed_string( "Hook" ), vector<hashed_string>{ hashed_string( "Hook" ) }, error ) );
    SW_EXPECT_FALSE( model.canUndo() );
    SW_EXPECT_FALSE( model.isDirty() );

    SW_ASSERT_TRUE( model.renameSocket( hashed_string( "Hook" ), hashed_string( "BeltHook" ), error ) );
    SW_ASSERT_EQUAL( size_t( 3 ), model.getSockets().getSockets().size() );
    SW_EXPECT_STREQ( "BeltHook", model.getSockets().getSockets()[0]._name.c_str() ); // 자리 유지
    const SocketDef* pScabbard = model.findSocket( hashed_string( "Scabbard" ) );
    SW_ASSERT_NOT_NULL( pScabbard );
    SW_ASSERT_EQUAL( size_t( 1 ), pScabbard->_listFallback.size() );
    SW_EXPECT_STREQ( "BeltHook", pScabbard->_listFallback[0].c_str() );

    vector<string> listIssue;
    SW_EXPECT_TRUE( model.validate( listIssue ) );

    SW_ASSERT_TRUE( model.removeSocket( hashed_string( "BeltHook" ), error ) );
    SW_EXPECT_FALSE( model.validate( listIssue ) ); // 후보가 가리키던 소켓이 사라졌다
    SW_EXPECT_EQUAL( size_t( 1 ), listIssue.size() );

    SW_ASSERT_TRUE( model.undo() );
    SW_ASSERT_TRUE( model.undo() );
    SW_EXPECT_NOT_NULL( model.findSocket( hashed_string( "Hook" ) ) );
    SW_EXPECT_FALSE( model.isDirty() ); // 처음 상태로 돌아왔다
    SW_ASSERT_TRUE( model.redo() );
    SW_EXPECT_NOT_NULL( model.findSocket( hashed_string( "BeltHook" ) ) );
}

/**
 * @brief [SocketEditModelTest] 본 목록이 있으면 없는 부모 본을 거절하고, 가상 본은 부모로 인정한다
 */
SW_TEST_CASE( SocketEditModelTest, ValidatesParentAgainstBoneNamesAndVirtualBones )
{
    using Internal = CharacterEditorModelTestInternal;

    SocketEditModel model = Internal::makeModel( "<SocketSet><VirtualBone name='AimRef' from='hand_r' to='hand_l' weight='0.5'/>"
                                                 "<Socket name='Muzzle' parent='hand_r' kind='Attach'/></SocketSet>" );
    string          error;
    // 본 목록이 없으면 부모를 검증하지 않는다.
    SW_EXPECT_TRUE( model.setParent( hashed_string( "Muzzle" ), hashed_string( "anything" ), error ) );

    model.setBoneNames( vector<hashed_string>{ hashed_string( "hand_r" ), hashed_string( "hand_l" ) } );
    SW_EXPECT_FALSE( model.setParent( hashed_string( "Muzzle" ), hashed_string( "nobone" ), error ) );
    SW_EXPECT_TRUE( model.setParent( hashed_string( "Muzzle" ), hashed_string( "hand_l" ), error ) );
    SW_EXPECT_TRUE( model.setParent( hashed_string( "Muzzle" ), hashed_string( "AimRef" ), error ) ); // 가상 본
    SW_EXPECT_TRUE( model.setParent( hashed_string( "Muzzle" ), hashed_string{}, error ) );           // 루트

    // 소켓을 지워도 가상 본은 남는다.
    SW_ASSERT_TRUE( model.removeSocket( hashed_string( "Muzzle" ), error ) );
    SW_EXPECT_NOT_NULL( model.getSockets().findVirtualBone( hashed_string( "AimRef" ) ) );
}

/**
 * @brief [SocketEditModelTest] 대칭은 짝 소켓을 만들고(부모 · 후보도 반대편으로), 가운데 이름은 거절하며, 이미 있으면 맞춘다
 */
SW_TEST_CASE( SocketEditModelTest, MirrorCreatesAndUpdatesCounterpart )
{
    using Internal = CharacterEditorModelTestInternal;

    SocketEditModel model = Internal::makeModel( "<SocketSet>"
                                                 "<Socket name='Palm_L' parent='hand_L' kind='Attach' translation='0.1 0.2 0.3' rotation='0 0 30' fallback='Knuckle_L'/>"
                                                 "<Socket name='Knuckle_L' parent='hand_L' kind='Attach'/>"
                                                 "<Socket name='Belly' parent='spine' kind='Attach'/>"
                                                 "</SocketSet>" );
    string          error;
    hashed_string   mirrorName;

    SW_EXPECT_FALSE( model.mirrorSocket( hashed_string( "Belly" ), mirrorName, error ) );
    SW_EXPECT_FALSE( model.canUndo() );

    SW_ASSERT_TRUE( model.mirrorSocket( hashed_string( "Palm_L" ), mirrorName, error ) );
    SW_EXPECT_STREQ( "Palm_R", mirrorName.c_str() );
    const SocketDef* pPalm = model.findSocket( hashed_string( "Palm_R" ) );
    SW_ASSERT_NOT_NULL( pPalm );
    SW_EXPECT_STREQ( "hand_R", pPalm->_parent.c_str() );
    SW_EXPECT_TRUE( Internal::isNear( float3( -0.1f, 0.2f, 0.3f ), pPalm->_translation, 1.0e-6f ) );
    SW_ASSERT_EQUAL( size_t( 1 ), pPalm->_listFallback.size() );
    SW_EXPECT_STREQ( "Knuckle_R", pPalm->_listFallback[0].c_str() );

    // 원본을 고친 뒤 다시 대칭하면 짝이 맞춰진다(소켓 수는 그대로).
    SW_ASSERT_TRUE( model.setTranslation( hashed_string( "Palm_L" ), float3( 0.5f, 0.0f, 0.0f ), false, error ) );
    SW_ASSERT_TRUE( model.mirrorSocket( hashed_string( "Palm_L" ), mirrorName, error ) );
    SW_EXPECT_EQUAL( size_t( 4 ), model.getSockets().getSockets().size() );
    SW_EXPECT_NEAR_EQUAL( -0.5f, model.findSocket( hashed_string( "Palm_R" ) )->_translation._x, 1.0e-6f );

    // 부모 본 목록이 있고 반대편 본이 없으면 거절한다.
    model.setBoneNames( vector<hashed_string>{ hashed_string( "hand_L" ) } );
    SW_EXPECT_FALSE( model.mirrorSocket( hashed_string( "Knuckle_L" ), mirrorName, error ) );
}

/**
 * @brief [SocketEditModelTest] 같은 필드의 연속 편집(끌기)은 되돌리기 한 단계이고, 다른 필드는 따로 쌓인다
 */
SW_TEST_CASE( SocketEditModelTest, ContinuousEditsCoalesceIntoOneUndoStep )
{
    using Internal = CharacterEditorModelTestInternal;

    SocketEditModel     model = Internal::makeModel( "<SocketSet><Socket name='Muzzle' parent='barrel' kind='Attach' translation='0 0 0'/></SocketSet>" );
    string              error;
    const hashed_string name( "Muzzle" );

    for ( int32 step = 1; step <= 5; ++step )
    {
        SW_ASSERT_TRUE( model.setTranslation( name, float3( 0.0f, 0.0f, 0.1f * static_cast<float32>( step ) ), true, error ) );
    }
    model.endContinuousEdit();
    SW_ASSERT_TRUE( model.setScale( name, float3( 2.0f ), true, error ) );

    SW_ASSERT_TRUE( model.undo() ); // 크기
    SW_EXPECT_NEAR_EQUAL( 1.0f, model.findSocket( name )->_scale._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, model.findSocket( name )->_translation._z, 1.0e-6f );
    SW_ASSERT_TRUE( model.undo() ); // 끌기 전체
    SW_EXPECT_NEAR_EQUAL( 0.0f, model.findSocket( name )->_translation._z, 1.0e-6f );
    SW_EXPECT_FALSE( model.canUndo() );
}

/**
 * @brief [SocketEditModelTest] 읽기에 실패하면 현재 내용을 그대로 두고, 다시 읽으면 기록을 지운다
 */
SW_TEST_CASE( SocketEditModelTest, FailedLoadKeepsContentAndLoadClearsHistory )
{
    using Internal = CharacterEditorModelTestInternal;

    SocketEditModel model = Internal::makeModel( "<SocketSet><Socket name='A' kind='Attach'/></SocketSet>" );
    string          error;
    SW_ASSERT_TRUE( model.addSocket( hashed_string( "B" ), hashed_string{}, hashed_string{}, error ) );
    SW_EXPECT_TRUE( model.canUndo() );

    SW_EXPECT_FALSE( model.loadFromXMLText( "<SocketSet><Socket name='X' kind='Unknown'/></SocketSet>", "bad.sockets.xml" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), model.getSockets().getSockets().size() );
    SW_EXPECT_TRUE( model.canUndo() );

    SW_ASSERT_TRUE( model.loadFromXMLText( "<SocketSet><Socket name='C' kind='Attach'/></SocketSet>", "good.sockets.xml" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), model.getSockets().getSockets().size() );
    SW_EXPECT_FALSE( model.canUndo() );
    SW_EXPECT_FALSE( model.isDirty() );
}

/**
 * @brief [AppearanceEditModelTest] 슬롯 · 꾸미기 값 편집은 되돌릴 수 있고, 같은 값은 기록을 남기지 않으며, 저장하면 더럽힘이 풀린다
 */
SW_TEST_CASE( AppearanceEditModelTest, EditsUndoRedoAndSaveToPresetStore )
{
    AppearanceEditModel model;
    SW_EXPECT_FALSE( model.isDirty() );

    SW_EXPECT_TRUE( model.setSlotItem( hashed_string( "Head" ), hashed_string( "Helmet" ) ) );
    SW_EXPECT_FALSE( model.setSlotItem( hashed_string( "Head" ), hashed_string( "Helmet" ) ) ); // 같은 값
    SW_EXPECT_STREQ( "Helmet", model.findSlotItem( hashed_string( "Head" ) ).c_str() );
    SW_EXPECT_TRUE( model.setSlotItem( hashed_string( "Head" ), hashed_string{} ) ); // 비움
    SW_EXPECT_EQUAL( size_t( 1 ), model.getSelection()._listSlot.size() );
    SW_EXPECT_TRUE( model.setBodyShape( hashed_string( "Slim" ) ) );
    SW_EXPECT_TRUE( model.setSeed( 7 ) );
    SW_EXPECT_FALSE( model.setSeed( 7 ) );

    // 슬라이더 끌기는 한 단계.
    const hashed_string height( "Height" );
    for ( int32 step = 1; step <= 4; ++step )
    {
        SW_EXPECT_TRUE( model.setNumber( height, 0.1f * static_cast<float32>( step ), true ) );
    }
    model.endContinuousEdit();
    SW_EXPECT_NEAR_EQUAL( 0.4f, model.getSelection()._customization.findValue( height )->_number._x, 1.0e-6f );
    SW_ASSERT_TRUE( model.undo() );
    SW_EXPECT_NULL( model.getSelection()._customization.findValue( height ) );
    SW_ASSERT_TRUE( model.redo() );
    SW_EXPECT_NEAR_EQUAL( 0.4f, model.getSelection()._customization.findValue( height )->_number._x, 1.0e-6f );

    SW_EXPECT_TRUE( model.setOption( hashed_string( "Hair" ), hashed_string( "Long" ) ) );
    SW_EXPECT_TRUE( model.removeValue( hashed_string( "Hair" ) ) );
    SW_EXPECT_FALSE( model.removeValue( hashed_string( "Hair" ) ) );
    SW_EXPECT_TRUE( model.removeSlot( hashed_string( "Head" ) ) );
    SW_EXPECT_FALSE( model.removeSlot( hashed_string( "Head" ) ) );

    SW_EXPECT_TRUE( model.isDirty() );
    UserAppearancePresetStore store;
    SW_EXPECT_FALSE( model.saveToPresetStore( store, "" ) );
    SW_ASSERT_TRUE( model.saveToPresetStore( store, "Hero" ) );
    SW_EXPECT_FALSE( model.isDirty() );
    SW_ASSERT_NOT_NULL( store.findPresetByName( "Hero" ) );
    SW_EXPECT_STREQ( "Slim", store.findPresetByName( "Hero" )->_selection._bodyShape.c_str() );

    SW_ASSERT_TRUE( model.undo() ); // 슬롯 제거를 되돌림 — 저장된 상태와 달라진다
    SW_EXPECT_TRUE( model.isDirty() );
    SW_ASSERT_TRUE( model.redo() );
    SW_EXPECT_FALSE( model.isDirty() );

    AppearanceSelection loaded;
    loaded._bodyType = hashed_string( "Male" );
    model.setSelection( loaded );
    SW_EXPECT_FALSE( model.canUndo() );
    SW_EXPECT_FALSE( model.isDirty() );
}
