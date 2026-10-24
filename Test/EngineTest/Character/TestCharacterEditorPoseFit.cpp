#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/Fit/FitSolver.h"

#include "EngineTest/AppearanceTestFixture.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/BonePoseEditModel.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterMirrorUtil.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/FitCheckSummary.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/SaveReloadGuard.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 캐릭터 편집 창 후속 모델 — 본 포즈 덮어쓰기 편집, 장비 입히기 · 규칙 설명, 프리셋 파일 저장, 피팅 요약.

namespace
{
    struct CharacterEditorPoseFitTestInternal
    {
        static constexpr const utf8* kMirrorXML = "<MirrorNames><Pair first='_L' second='_R'/></MirrorNames>";

        static MirrorNameRules makeRules()
        {
            MirrorNameRules rules;
            const bool      bLoaded = rules.loadFromXMLText( kMirrorXML, "test.mirrornames.xml" );
            SW_ASSERT( bLoaded );
            (void)bLoaded;
            return rules;
        }

        /** @brief root - spine - arm_L / arm_R 네 본짜리 스켈레톤입니다. */
        static Skeleton makeSkeleton()
        {
            Skeleton      skeleton;
            BoneTransform pose;
            const int32   root  = skeleton.addBone( hashed_string( "root" ), -1, pose, float4x4::Identity );
            const int32   spine = skeleton.addBone( hashed_string( "spine" ), root, pose, float4x4::Identity );
            pose._translation   = float3( 0.2f, 0.0f, 0.0f );
            (void)skeleton.addBone( hashed_string( "arm_L" ), spine, pose, float4x4::Identity );
            pose._translation = float3( -0.2f, 0.0f, 0.0f );
            (void)skeleton.addBone( hashed_string( "arm_R" ), spine, pose, float4x4::Identity );
            skeleton.computeInverseBindFromReference();
            return skeleton;
        }

        static bool isNear( const float3& expected, const float3& actual, float32 tolerance ) { return float3::getDistance( expected, actual ) <= tolerance; }
    };
} // namespace

/**
 * @brief [BonePoseEditModelTest] 본 목록과 트리를 스켈레톤에서 읽고, 편집은 덮어쓰기에만 쌓이며 스켈레톤은 바뀌지 않는다
 */
SW_TEST_CASE( BonePoseEditModelTest, EditsOverrideOnlyAndKeepsSkeleton )
{
    using Internal = CharacterEditorPoseFitTestInternal;

    BonePoseEditModel model;
    model.setSkeleton( Internal::makeSkeleton() );

    vector<hashed_string> listName;
    model.collectBoneNames( listName );
    SW_ASSERT_EQUAL( size_t( 4 ), listName.size() );
    SW_EXPECT_STREQ( "arm_R", listName[3].c_str() );

    vector<uint32> listChild;
    model.collectChildren( -1, listChild );
    SW_EXPECT_EQUAL( size_t( 1 ), listChild.size() );
    model.collectChildren( 1, listChild );
    SW_EXPECT_EQUAL( size_t( 2 ), listChild.size() );

    const hashed_string arm( "arm_L" );
    string              error;
    SW_EXPECT_FALSE( model.isDirty() );
    SW_ASSERT_TRUE( model.setTranslation( arm, float3( 0.5f, 0.0f, 0.0f ), false, error ) );
    SW_EXPECT_TRUE( model.isDirty() );
    SW_EXPECT_TRUE( model.hasOverride( arm ) );

    BoneTransform effective;
    SW_ASSERT_TRUE( model.getEffectiveTransform( arm, effective ) );
    SW_EXPECT_TRUE( Internal::isNear( float3( 0.5f, 0.0f, 0.0f ), effective._translation, 1.0e-6f ) );
    SW_EXPECT_NEAR_EQUAL( 0.2f, model.getSkeleton().getBone( 2 )._referencePose._translation._x, 1.0e-6f ); // 임포트 값은 그대로

    // 같은 값은 기록을 남기지 않고, 없는 본은 오류를 남긴다.
    error.clear();
    SW_EXPECT_FALSE( model.setTranslation( arm, float3( 0.5f, 0.0f, 0.0f ), false, error ) );
    SW_EXPECT_TRUE( error.empty() );
    SW_EXPECT_FALSE( model.setTranslation( hashed_string( "tail" ), float3( 1.0f ), false, error ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_FALSE( model.hasOverride( hashed_string( "tail" ) ) );

    // 적은 필드만 저장된다 — 위치만 고쳤으니 회전 · 스케일 속성은 없다.
    const string text = model.saveToXMLText();
    SW_EXPECT_TRUE( text.find( "translation" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "rotation" ) == string::npos );
    SW_EXPECT_TRUE( text.find( "scale" ) == string::npos );
}

/**
 * @brief [BonePoseEditModelTest] 끌기는 한 단계로 합쳐지고, 되돌리기 · 지우기 · 좌우 대칭이 덮어쓰기를 정확히 되돌리거나 만든다
 */
SW_TEST_CASE( BonePoseEditModelTest, UndoClearAndMirrorBones )
{
    using Internal = CharacterEditorPoseFitTestInternal;

    BonePoseEditModel model;
    model.setSkeleton( Internal::makeSkeleton() );
    const MirrorNameRules rules = Internal::makeRules();
    const hashed_string   left( "arm_L" );
    const hashed_string   right( "arm_R" );
    string                error;

    for ( int32 step = 1; step <= 4; ++step )
    {
        SW_ASSERT_TRUE( model.setRotationDegrees( left, float3( 0.0f, 0.0f, 10.0f * static_cast<float32>( step ) ), true, error ) );
    }
    model.endContinuousEdit();
    SW_ASSERT_TRUE( model.undo() ); // 끌기 전체가 한 단계
    SW_EXPECT_FALSE( model.hasOverride( left ) );
    SW_EXPECT_FALSE( model.canUndo() );
    SW_ASSERT_TRUE( model.redo() );

    // 반대편은 본 이름 규칙으로 찾고, 위치는 축 성분이 뒤집힌다.
    SW_ASSERT_TRUE( model.setTranslation( left, float3( 0.4f, 0.1f, 0.0f ), false, error ) );
    hashed_string mirrored;
    SW_ASSERT_TRUE( model.mirrorBone( left, rules, mirrored, error ) );
    SW_EXPECT_STREQ( "arm_R", mirrored.c_str() );
    const BoneOverride* pRight = model.getOverride().findOverride( right );
    SW_ASSERT_NOT_NULL( pRight );
    SW_EXPECT_TRUE( Internal::isNear( float3( -0.4f, 0.1f, 0.0f ), pRight->_translation, 1.0e-5f ) );

    // 짝 토큰이 없는 본 · 스켈레톤에 없는 반대편 · 덮어쓰기가 없는 본은 실패한다.
    error.clear();
    SW_EXPECT_FALSE( model.mirrorBone( hashed_string( "spine" ), rules, mirrored, error ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_ASSERT_TRUE( model.clearBone( right, error ) );
    SW_EXPECT_FALSE( model.hasOverride( right ) );
    SW_EXPECT_FALSE( model.clearBone( right, error ) );
    SW_ASSERT_TRUE( model.undo() ); // 지우기 되돌림
    SW_EXPECT_TRUE( model.hasOverride( right ) );
}

/**
 * @brief [BonePoseEditModelTest] 덮어쓰기는 파일로 왕복하고, 읽기에 실패하면 현재 내용을 그대로 둔다
 */
SW_TEST_CASE( BonePoseEditModelTest, RoundTripsThroughFile )
{
    using Internal = CharacterEditorPoseFitTestInternal;

    BonePoseEditModel model;
    model.setSkeleton( Internal::makeSkeleton() );
    string error;
    SW_ASSERT_TRUE( model.setScale( hashed_string( "spine" ), float3( 1.0f, 1.2f, 1.0f ), false, error ) );

    const string path = test::makeTempPath( "edit.refpose.xml" );
    SW_ASSERT_TRUE( model.saveToFile( path ) );
    SW_EXPECT_FALSE( model.isDirty() );

    BonePoseEditModel reloaded;
    reloaded.setSkeleton( Internal::makeSkeleton() );
    SW_ASSERT_TRUE( reloaded.loadFromFile( path ) );
    BoneTransform effective;
    SW_ASSERT_TRUE( reloaded.getEffectiveTransform( hashed_string( "spine" ), effective ) );
    SW_EXPECT_NEAR_EQUAL( 1.2f, effective._scale._y, 1.0e-6f );
    SW_EXPECT_FALSE( reloaded.isDirty() );

    SW_EXPECT_FALSE( reloaded.loadFromXMLText( "<ReferencePose><Bone name='spine' weird='1'/></ReferencePose>", "bad.refpose.xml" ) );
    SW_EXPECT_TRUE( reloaded.hasOverride( hashed_string( "spine" ) ) );
}

/**
 * @brief [AppearanceEditModelDatabaseTest] 장비 입히기는 슬롯 표와 세트로 걸러 넘기고, 입혀 보면 규칙 설명이 나온다
 */
SW_TEST_CASE( AppearanceEditModelDatabaseTest, EquipSetAndExplainWithDatabase )
{
    appearancetest::Fixture fixture;
    SW_ASSERT_TRUE( fixture.load() );

    // 머리 칸에 낄 수 있는 아이템만 나온다(카탈로그의 Head 장비).
    vector<hashed_string> listItem;
    AppearanceEditModel::collectSlotItems( fixture._database, hashed_string( "Head" ), listItem );
    SW_EXPECT_EQUAL( size_t( 3 ), listItem.size() ); // helm, cap, crown
    // 주 손 칸은 accept="Weapon" 이므로 무기 종류가 나온다.
    AppearanceEditModel::collectSlotItems( fixture._database, hashed_string( "MainHand" ), listItem );
    SW_EXPECT_EQUAL( size_t( 3 ), listItem.size() ); // sword, greatsword, shield

    AppearanceEditModel model;
    AppearanceSelection selection;
    selection._basePresetID = hashed_string( "Human" );
    model.setSelection( selection );

    SW_EXPECT_FALSE( model.equipSet( fixture._database, hashed_string( "NoSuchSet" ) ) );
    SW_ASSERT_TRUE( model.equipSet( fixture._database, hashed_string( "Knight" ) ) );
    SW_EXPECT_STREQ( "helm", model.findSlotItem( hashed_string( "Head" ) ).c_str() );
    SW_EXPECT_STREQ( "plate", model.findSlotItem( hashed_string( "Body" ) ).c_str() );
    SW_EXPECT_STREQ( "greaves", model.findSlotItem( hashed_string( "Legs" ) ).c_str() );
    SW_EXPECT_FALSE( model.equipSet( fixture._database, hashed_string( "Knight" ) ) ); // 이미 입었다

    // 몸 종류가 여성이면 몸통 조각이 바뀐다.
    SW_ASSERT_TRUE( model.setBodyType( hashed_string( "Female" ) ) );
    SW_ASSERT_TRUE( model.equipSet( fixture._database, hashed_string( "Knight" ) ) );
    SW_EXPECT_STREQ( "plate_f", model.findSlotItem( hashed_string( "Body" ) ).c_str() );

    // 한 번 입히기는 되돌리기 한 단계다.
    SW_ASSERT_TRUE( model.undo() );
    SW_EXPECT_STREQ( "plate", model.findSlotItem( hashed_string( "Body" ) ).c_str() );

    // 풀 투구는 머리카락을 숨기는 규칙을 설명에 남긴다.
    ResolvedAppearance        resolved;
    AppearanceSelectionReport report;
    model.previewResolved( fixture._database, resolved, report );
    SW_EXPECT_TRUE( resolved.countTrace( hashed_string( "FullHelmHidesHair" ) ) > 0 );
    SW_EXPECT_TRUE( report._bUnknownBasePreset == SW_FALSE );
}

/**
 * @brief [AppearanceEditModelDatabaseTest] 프리셋은 파일에 저장되고 다시 열어도 같은 선택이며, 없는 이름은 열리지 않는다
 */
SW_TEST_CASE( AppearanceEditModelDatabaseTest, SavesAndReopensPresetFile )
{
    appearancetest::Fixture fixture;
    SW_ASSERT_TRUE( fixture.load() );

    AppearanceEditModel model;
    AppearanceSelection selection;
    selection._basePresetID = hashed_string( "Human" );
    selection._schema       = hashed_string( "Human" );
    model.setSelection( selection );
    SW_ASSERT_TRUE( model.equipSet( fixture._database, hashed_string( "Knight" ) ) );
    SW_ASSERT_TRUE( model.setNumber( hashed_string( "Height" ), 1.5f, false ) );

    UserAppearancePresetStore store;
    store.setDatabase( &fixture._database );
    const string path = test::makeTempPath( "userpresets.txt" );
    SW_EXPECT_FALSE( model.saveToPresetFile( store, "", path ) );
    SW_ASSERT_TRUE( model.saveToPresetFile( store, "Hero", path ) );
    SW_EXPECT_FALSE( model.isDirty() );

    UserAppearancePresetStore reopened;
    reopened.setDatabase( &fixture._database );
    SW_ASSERT_TRUE( AppearanceEditModel::loadPresetFile( reopened, path ) );
    AppearanceEditModel second;
    SW_ASSERT_TRUE( second.loadFromPreset( reopened, "Hero" ) );
    SW_EXPECT_STREQ( "helm", second.findSlotItem( hashed_string( "Head" ) ).c_str() );
    SW_ASSERT_NOT_NULL( second.getSelection()._customization.findValue( hashed_string( "Height" ) ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, second.getSelection()._customization.findValue( hashed_string( "Height" ) )->_number._x, 0.01f );
    SW_EXPECT_FALSE( second.loadFromPreset( reopened, "Nobody" ) );
    SW_EXPECT_FALSE( AppearanceEditModel::loadPresetFile( reopened, test::makeTempPath( "missing.presets.txt" ) ) );
}

/**
 * @brief [FitCheckSummaryTest] 피팅 결과를 부품별 잘린 면 비율 · 조임 강도와 관통 점 수 · 깊이로 요약한다
 */
SW_TEST_CASE( FitCheckSummaryTest, SummarizesCutsDeltasAndPenetrations )
{
    FitResult     result;
    FitPartResult body;
    body._name = hashed_string( "Body" );
    body.initialize( 8, 3 );
    FitPartResult shirt;
    shirt._name = hashed_string( "Shirt" );
    shirt.initialize( 10, 4 );
    shirt._cutTriangleCount   = 4;
    shirt._listVertexDelta[1] = float3( 0.0f, 0.03f, 0.0f );
    shirt._listVertexDelta[3] = float3( 0.04f, 0.0f, 0.0f );
    result._listPart.push_back( body );
    result._listPart.push_back( shirt );
    FitPenetration penetration;
    penetration._partA       = 0;
    penetration._partB       = 1;
    penetration._vertexCount = 6;
    penetration._maxDepth    = 0.02f;
    penetration._listPoint.push_back( float3( 0.1f, 1.2f, 0.3f ) );
    result._report._listPenetration.push_back( penetration );
    result._report.addMessage( "Character.FitRigidPenetration", "shirt: pushed out 2 vertices" );

    const FitCheckSummary summary = FitCheckSummary::fromResult( result );
    SW_ASSERT_EQUAL( size_t( 2 ), summary._listPart.size() );
    SW_EXPECT_FALSE( summary._listPart[0].isCut() );
    SW_EXPECT_EQUAL( 0u, summary._listPart[0]._movedVertexCount );
    SW_EXPECT_TRUE( summary._listPart[1].isCut() );
    SW_EXPECT_NEAR_EQUAL( 0.4f, summary._listPart[1]._cutRatio, 1.0e-6f );
    SW_EXPECT_EQUAL( 2u, summary._listPart[1]._movedVertexCount );
    SW_EXPECT_NEAR_EQUAL( 0.04f, summary._listPart[1]._maxDelta, 1.0e-6f );

    SW_EXPECT_TRUE( summary.hasPenetration() );
    SW_ASSERT_EQUAL( size_t( 1 ), summary._listPenetration.size() );
    SW_EXPECT_STREQ( "Body", summary._listPenetration[0]._partA.c_str() );
    SW_EXPECT_STREQ( "Shirt", summary._listPenetration[0]._partB.c_str() );
    SW_EXPECT_EQUAL( 6u, summary._penetrationVertexCount );
    SW_ASSERT_EQUAL( size_t( 1 ), summary._listPenetration[0]._listPoint.size() ); // 뷰포트에 찍을 위치가 요약까지 온다
    SW_EXPECT_NEAR_EQUAL( 1.2f, summary._listPenetration[0]._listPoint[0]._y, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.02f, summary._maxPenetrationDepth, 1.0e-6f );
    SW_EXPECT_EQUAL( size_t( 1 ), summary._listMessage.size() );

    SW_EXPECT_FALSE( FitCheckSummary::fromResult( FitResult{} ).hasPenetration() );
}

/**
 * @brief [SaveReloadGuardTest] 저장 직후의 다시 읽기는 무시하고, 그 밖의 증가는 한 번만 외부 변경으로 알린다
 */
SW_TEST_CASE( SaveReloadGuardTest, IgnoresOwnWriteOnceAndReportsExternalChange )
{
    SaveReloadGuard guard;
    guard.reset( 5 );
    SW_EXPECT_FALSE( guard.pollExternalChange( 5, 1.0 ) );

    guard.onSaved( 5, 10.0 );
    SW_EXPECT_FALSE( guard.pollExternalChange( 6, 10.5 ) ); // 우리 저장이 부른 다시 읽기
    SW_EXPECT_TRUE( guard.pollExternalChange( 7, 11.0 ) );  // 그 뒤의 증가는 외부 변경
    SW_EXPECT_FALSE( guard.pollExternalChange( 7, 11.5 ) ); // 알림은 한 번

    guard.onSaved( 7, 20.0 );
    SW_EXPECT_FALSE( guard.pollExternalChange( 7, 30.0 ) ); // 다시 읽기가 오지 않으면 무시 몫을 거둔다
    SW_EXPECT_TRUE( guard.pollExternalChange( 8, 31.0 ) );

    guard.onSaved( 8, 40.0 );
    SW_EXPECT_TRUE( guard.pollExternalChange( 9, 40.0 + SaveReloadGuard::kOwnWriteWindowSeconds + 1.0 ) ); // 창이 지난 뒤는 외부
}
