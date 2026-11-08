#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Skeletal/Skeleton.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetPreviewModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileEdit.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileInspection.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct RetargetEditTestUtil
    {
        /** @brief root → hips → thigh → shin 스켈레톤입니다. 뼈마다 위로 1 m 입니다. */
        static Skeleton makeSkeleton()
        {
            Skeleton      skeleton;
            BoneTransform local{};
            local._translation = float3{ 0.0f, 1.0f, 0.0f };
            int32 parent       = -1;
            for ( const utf8* pName : { "root", "hips", "thigh", "shin" } )
            {
                parent = skeleton.addBone( hashed_string( pName ), parent, local );
            }
            return skeleton;
        }

        /** @brief 두 스켈레톤에 묶이는 프로필입니다. */
        static RetargetProfile makeProfile()
        {
            RetargetProfile profile;
            profile.setRootAndPelvis( hashed_string( "root" ), hashed_string( "root" ), hashed_string( "hips" ), hashed_string( "hips" ), RetargetTranslationMode::Copy );
            RetargetChain chain{};
            chain._name           = hashed_string( "Leg" );
            chain._listSourceBone = { hashed_string( "thigh" ), hashed_string( "shin" ) };
            chain._listTargetBone = { hashed_string( "thigh" ), hashed_string( "shin" ) };
            profile.addChain( chain );
            return profile;
        }

        static bool containsText( const vector<string>& listLine, const char* pText )
        {
            for ( const string& line : listLine )
            {
                if ( line.find( pText ) != string::npos )
                    return true;
            }
            return false;
        }
    };
    using Util = RetargetEditTestUtil;
} // namespace

SW_TEST_CASE( RetargetProfileEditTest, NamesRoundTripThroughText )
{
    const vector<hashed_string> listName = RetargetProfileEdit::parseNames( "spine, chest,neck" );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listName.size() ) );
    SW_EXPECT_TRUE( listName[1] == hashed_string( "chest" ) );
    const vector<hashed_string> listAgain = RetargetProfileEdit::parseNames( RetargetProfileEdit::joinNames( listName ) );
    SW_EXPECT_TRUE( listAgain == listName );
}

// 빈 이름 · 겹친 이름은 다시 읽을 때 오류라 더하지 않는다.
SW_TEST_CASE( RetargetProfileEditTest, ChainNamesMustBeNewAndNotEmpty )
{
    vector<RetargetChain> listChain;
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddChain( listChain, hashed_string{} ) );
    SW_EXPECT_TRUE( RetargetProfileEdit::canAddChain( listChain, hashed_string( "Arm" ) ) );
    RetargetChain chain{};
    chain._name = hashed_string( "Arm" );
    listChain.push_back( chain );
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddChain( listChain, hashed_string( "Arm" ) ) );
}

SW_TEST_CASE( RetargetProfileEditTest, APoseBoneIsListedOnce )
{
    vector<RetargetPoseBone> listPose;
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddPoseBone( listPose, hashed_string{} ) );
    SW_EXPECT_TRUE( RetargetProfileEdit::canAddPoseBone( listPose, hashed_string( "hip" ) ) );
    listPose.push_back( RetargetPoseBone{ hashed_string( "hip" ), float3{} } );
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddPoseBone( listPose, hashed_string( "hip" ) ) );
}

// 본 목록이 빈 사슬은 `loadFromXMLText` 이 거절한다 — 만들 수 있으면 되돌리기가 그 글로 돌아올 때 프로필이 통째로 비었다.
SW_TEST_CASE( RetargetProfileEditTest, AChainIsMadeOnlyWithBonesOnBothSides )
{
    vector<RetargetChain> listChain;
    RetargetChain         chain{};
    SW_EXPECT_FALSE( RetargetProfileEdit::makeChain( listChain, "Leg", "", "thigh", chain ) );
    SW_EXPECT_FALSE( RetargetProfileEdit::makeChain( listChain, "Leg", "thigh", " , ", chain ) );
    SW_EXPECT_FALSE( RetargetProfileEdit::makeChain( listChain, "", "thigh", "thigh", chain ) );
    SW_REQUIRE_TRUE( RetargetProfileEdit::makeChain( listChain, "Leg", "thigh, shin", "thigh", chain ) );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( chain._listSourceBone.size() ) );

    RetargetProfile profile = Util::makeProfile();
    profile.setSkeletonPaths( "a.skeleton.xml", "b.skeleton.xml" );
    profile.addChain( RetargetChain{ hashed_string( "Empty" ), {}, {}, SW_FALSE } );
    RetargetProfile reread;
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( reread.loadFromXMLText( profile.toXML(), "empty chain" ) ); // 이 모양이 왜 막혔는지 그대로 적어 둔다
}

// 만든 사슬로 이룬 프로필은 쓴 글을 그대로 다시 읽는다 — 되돌리기의 전제다.
SW_TEST_CASE( RetargetProfileEditTest, ProfilesTheEditorBuildsRoundTripThroughXML )
{
    RetargetProfile profile = Util::makeProfile();
    profile.setSkeletonPaths( "a.skeleton.xml", "b.skeleton.xml" );
    profile.setTargetPose( {
        RetargetPoseBone{ hashed_string( "shin" ), float3{ 1.0f, 2.0f, 3.0f } }
    } );
    RetargetProfile reread;
    SW_REQUIRE_TRUE( reread.loadFromXMLText( profile.toXML(), "round trip" ) );
    SW_EXPECT_TRUE( reread.toXML() == profile.toXML() );

    RetargetProfile blank; // 새 문서는 아직 아무것도 묶이지 않았어도 되돌리기로 돌아온다
    SW_REQUIRE_TRUE( reread.loadFromXMLText( blank.toXML(), "blank" ) );
    SW_EXPECT_TRUE( reread.getChains().empty() );
}

SW_TEST_CASE( RetargetProfileEditTest, RenamingAChainRefusesATakenName )
{
    vector<RetargetChain> listChain;
    listChain.push_back( RetargetChain{ hashed_string( "Arm" ), {}, {}, SW_FALSE } );
    listChain.push_back( RetargetChain{ hashed_string( "Leg" ), {}, {}, SW_FALSE } );
    SW_EXPECT_FALSE( RetargetProfileEdit::renameChain( listChain, 0, hashed_string( "Leg" ) ) );
    SW_EXPECT_FALSE( RetargetProfileEdit::renameChain( listChain, 0, hashed_string{} ) );
    SW_EXPECT_FALSE( RetargetProfileEdit::renameChain( listChain, 9, hashed_string( "Tail" ) ) );
    SW_EXPECT_TRUE( listChain[0]._name == hashed_string( "Arm" ) );
    SW_EXPECT_TRUE( RetargetProfileEdit::renameChain( listChain, 0, hashed_string( "Tail" ) ) );
    SW_EXPECT_TRUE( listChain[0]._name == hashed_string( "Tail" ) );
}

// 같은 대상 본을 두 줄이 짝지으면 런타임은 앞 줄만 쓰고 뒤 줄을 말없이 버린다.
SW_TEST_CASE( RetargetProfileEditTest, ABonePairClaimsItsTargetOnce )
{
    vector<RetargetBonePair> listPair;
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddBonePair( listPair, hashed_string{}, hashed_string( "Head" ) ) );
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddBonePair( listPair, hashed_string( "head" ), hashed_string{} ) );
    SW_EXPECT_TRUE( RetargetProfileEdit::canAddBonePair( listPair, hashed_string( "head" ), hashed_string( "Head" ) ) );
    listPair.push_back( RetargetBonePair{ hashed_string( "head" ), hashed_string( "Head" ) } );
    SW_EXPECT_FALSE( RetargetProfileEdit::canAddBonePair( listPair, hashed_string( "neck" ), hashed_string( "Head" ) ) );
    SW_EXPECT_TRUE( RetargetProfileEdit::canAddBonePair( listPair, hashed_string( "head" ), hashed_string( "Neck" ) ) ); // 한 원본을 여러 대상에 옮기는 것은 된다
}

// ---------------------------------------------------------------------- 점검

SW_TEST_CASE( RetargetProfileInspectionTest, AGoodProfileReportsLengthsAndMappedBones )
{
    const Skeleton            skeleton = Util::makeSkeleton();
    RetargetProfileInspection inspection;
    SW_REQUIRE_TRUE( RetargetProfileInspection::inspect( Util::makeProfile(), &skeleton, &skeleton, inspection ).has_value() );
    SW_EXPECT_TRUE( inspection._listProblem.empty() );
    SW_EXPECT_TRUE( inspection._listWarning.empty() );
    SW_EXPECT_EQUAL( 4u, inspection._mappedTargetBoneCount );
    SW_EXPECT_EQUAL( 4u, inspection._targetBoneCount );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( inspection._listChain.size() ) );
    SW_EXPECT_NEAR( 1.0f, inspection._listChain[0]._sourceLength, 1e-4f );
    SW_EXPECT_NEAR( 1.0f, inspection._listChain[0].computeLengthRatio(), 1e-4f );
}

// 없는 본은 줄마다 이름을 말한다. 리타기터를 부르지 않으므로 치는 동안 오류 로그가 쌓이지 않는다(로그가 나오면 이 테스트가 실패한다).
SW_TEST_CASE( RetargetProfileInspectionTest, MissingBonesAreNamedWithoutErrorLogs )
{
    const Skeleton  skeleton = Util::makeSkeleton();
    RetargetProfile profile  = Util::makeProfile();
    profile.addBonePair( RetargetBonePair{ hashed_string( "nope_source" ), hashed_string( "nope_target" ) } );
    profile.setTargetPose( {
        RetargetPoseBone{ hashed_string( "nope_pose" ), float3{} }
    } );
    RetargetProfileInspection inspection;
    const expected<void>      missing = RetargetProfileInspection::inspect( profile, &skeleton, &skeleton, inspection );
    SW_REQUIRE_FALSE( missing.has_value() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( inspection._listProblem.size() ) );
    SW_EXPECT_TRUE( Util::containsText( inspection._listProblem, "nope_source" ) );
    SW_EXPECT_TRUE( Util::containsText( inspection._listProblem, "nope_target" ) );
    SW_EXPECT_TRUE( Util::containsText( inspection._listProblem, "nope_pose" ) );
    SW_EXPECT_TRUE( missing.error().detail() == string_view( inspection._listProblem.front() ) );

    SW_EXPECT_FALSE( RetargetProfileInspection::inspect( RetargetProfile{}, &skeleton, &skeleton, inspection ).has_value() ); // 빈 칸도 까닭을 말한다
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( inspection._listProblem.size() ) );

    const expected<void> noSource = RetargetProfileInspection::inspect( profile, nullptr, &skeleton, inspection );
    SW_REQUIRE_FALSE( noSource.has_value() );
    SW_EXPECT_TRUE( noSource.error().message().find( "source skeleton" ) != std::string_view::npos );
}

// 뒤집힌 사슬 · 겹친 대상 본 · 본 하나짜리 IK 는 묶이지만 뜻대로 돌지 않는다 — 저장은 막지 않고 알린다.
SW_TEST_CASE( RetargetProfileInspectionTest, SuspiciousMappingsWarnButStillBind )
{
    const Skeleton  skeleton = Util::makeSkeleton();
    RetargetProfile profile;
    profile.setRootAndPelvis( hashed_string( "root" ), hashed_string( "root" ), hashed_string( "hips" ), hashed_string( "hips" ), RetargetTranslationMode::Copy );
    RetargetChain reversed{};
    reversed._name           = hashed_string( "Reversed" );
    reversed._listSourceBone = { hashed_string( "shin" ), hashed_string( "thigh" ) };
    reversed._listTargetBone = { hashed_string( "thigh" ), hashed_string( "shin" ) };
    reversed._bIKGoal        = SW_FALSE;
    profile.addChain( reversed );
    RetargetChain shortGoal{};
    shortGoal._name           = hashed_string( "ShortGoal" );
    shortGoal._listSourceBone = { hashed_string( "shin" ) };
    shortGoal._listTargetBone = { hashed_string( "shin" ) }; // Reversed 가 이미 짝지은 대상 본
    shortGoal._bIKGoal        = SW_TRUE;
    profile.addChain( shortGoal );

    RetargetProfileInspection inspection;
    SW_REQUIRE_TRUE( RetargetProfileInspection::inspect( profile, &skeleton, &skeleton, inspection ).has_value() );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( inspection._listWarning.size() ) );
    SW_EXPECT_TRUE( Util::containsText( inspection._listWarning, "source bones do not run" ) );
    SW_EXPECT_TRUE( Util::containsText( inspection._listWarning, "IK goal" ) );
    SW_EXPECT_TRUE( Util::containsText( inspection._listWarning, "'shin' is mapped more than once" ) );
}

// ---------------------------------------------------------------------- 미리보기

// 값 하나를 고쳐도 스켈레톤 경로가 그대로면 디스크를 읽지 않고 카메라를 되돌리지 않는다.
SW_TEST_CASE( RetargetPreviewModelTest, OnlyASkeletonPathChangeIsStaleAndFramesTheCamera )
{
    const Skeleton  skeleton = Util::makeSkeleton();
    RetargetProfile profile  = Util::makeProfile();
    profile.setSkeletonPaths( "a.skeleton.xml", "b.skeleton.xml" );
    RetargetPreviewModel preview;
    SW_EXPECT_TRUE( preview.isStale( profile ) );
    SW_EXPECT_FALSE( preview.isAnyReady() );
    float3  center{};
    float32 radius = 0.0f;
    SW_EXPECT_FALSE( preview.findFrameSphere( center, radius ) );

    preview.applySkeletons( profile, &skeleton, &skeleton );
    SW_EXPECT_FALSE( preview.isStale( profile ) );
    SW_EXPECT_TRUE( preview.consumeFramePending() );
    SW_EXPECT_FALSE( preview.consumeFramePending() );

    profile.setMapSameNames( true );
    profile.setHeightRatioOverride( 2.0f );
    SW_EXPECT_FALSE( preview.isStale( profile ) );
    preview.rebuildPoses( profile );
    SW_EXPECT_FALSE( preview.consumeFramePending() ); // 자세를 다시 만들어도 카메라는 그대로다

    profile.setSkeletonPaths( "a.skeleton.xml", "c.skeleton.xml" );
    SW_EXPECT_TRUE( preview.isStale( profile ) );
}

SW_TEST_CASE( RetargetPreviewModelTest, RetargetPoseMovesOnlyThePosedTarget )
{
    const Skeleton  skeleton = Util::makeSkeleton();
    RetargetProfile profile  = Util::makeProfile();
    profile.setTargetPose( {
        RetargetPoseBone{ hashed_string( "root" ), float3{ 90.0f, 0.0f, 0.0f } }
    } );
    RetargetPreviewModel preview;
    preview.applySkeletons( profile, &skeleton, &skeleton );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( preview.getTargetPosePositions().size() ) );
    const float3 end       = preview.getTargetPosePositions().back();
    const float3 reference = preview.getTargetReferencePositions().back();
    SW_EXPECT_NEAR( 4.0f, reference._y, 1e-4f );
    SW_EXPECT_TRUE( ( end - reference ).getLength() > 0.5f );
    SW_EXPECT_NEAR( 4.0f, preview.getSourcePositions().back()._y, 1e-4f );

    // 끄는 동안 프로필을 바꾸고 다시 만들면 바로 따라간다.
    profile.setTargetPose( {} );
    preview.rebuildPoses( profile );
    SW_EXPECT_NEAR( 0.0f, ( preview.getTargetPosePositions().back() - reference ).getLength(), 1e-4f );

    // 없는 본을 적었으면 얹지 못하고 레퍼런스 그대로다 — 이유는 점검 칸이 말하므로 오류 로그를 남기지 않는다.
    profile.setTargetPose( {
        RetargetPoseBone{ hashed_string( "nope" ), float3{ 90.0f, 0.0f, 0.0f } }
    } );
    preview.rebuildPoses( profile );
    SW_EXPECT_NEAR( 0.0f, ( preview.getTargetPosePositions().back() - reference ).getLength(), 1e-4f );
}

SW_TEST_CASE( RetargetPreviewModelTest, FrameSphereHoldsEveryBoneAndAMissingSideIsNotReady )
{
    const Skeleton       skeleton = Util::makeSkeleton();
    RetargetPreviewModel preview;
    preview.applySkeletons( Util::makeProfile(), nullptr, &skeleton );
    SW_EXPECT_FALSE( preview.isSourceReady() );
    SW_EXPECT_TRUE( preview.isTargetReady() );
    SW_EXPECT_TRUE( preview.getSourcePositions().empty() );
    SW_EXPECT_FALSE( preview.hasSourceBone( hashed_string( "hips" ) ) );
    SW_EXPECT_TRUE( preview.hasTargetBone( hashed_string( "hips" ) ) );
    SW_EXPECT_FALSE( preview.hasTargetBone( hashed_string( "nope" ) ) );

    float3  center{};
    float32 radius = 0.0f;
    SW_REQUIRE_TRUE( preview.findFrameSphere( center, radius ) );
    SW_EXPECT_NEAR( 2.5f, center._y, 1e-4f ); // 본은 1 ~ 4 m 높이에 있다
    SW_EXPECT_NEAR( 1.5f * RetargetPreviewModel::kFramePadding, radius, 1e-4f );
}
