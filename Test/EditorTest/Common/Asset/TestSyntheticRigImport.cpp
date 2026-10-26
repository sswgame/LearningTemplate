#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "EditorTest/Common/Asset/SyntheticRigTestUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

// SyntheticRigImportTest — 합성 리그 생성기(`Scripts/dev/MakeSyntheticRig.py`)의 출력이 엔진 임포터 · 리그 로더로 읽힌다.

/**
 * @brief [SyntheticRigImportTest] 대표 합성 리그는 모두 임포트된다 — 본 수가 파일 이름(`_bNNN`)과 같고, 메시 · 클립에 모프가 실리고, 옆 리그가 그 스켈레톤에 묶인다
 * @details 생성기의 단위 테스트(`PythonTest_TestMakeSyntheticRig`)가 glTF 구조와 결정성을, 이 테스트가 "엔진이 읽는 범위 안" 을 지킨다.
 *          클립은 관절 수만큼 트랙을, 모프 타깃 수만큼 가중치 커브(`morph_NN`)를 가진다.
 */
SW_TEST_CASE( SyntheticRigImportTest, RepresentativesImportAndBindTheirRig )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::vector<sw::string> listPath = test::collectSyntheticRigPaths();
    SW_ASSERT_TRUE( listPath.empty() == false );
    for ( const sw::string& glbPath : listPath )
    {
        test::SyntheticRig rig;
        SW_ASSERT_TRUE_MSG( test::loadSyntheticRig( glbPath, rig ), glbPath.c_str() );
        const size_t boneMark = rig._stem.rfind( "_b" );
        SW_ASSERT_TRUE( boneMark != sw::string::npos );
        const std::string_view afterMark( rig._stem.c_str() + boneMark + 2 );
        int32                  bonesInNameValue = 0;
        SW_ASSERT_TRUE_MSG( sw::StringUtil::parseInt( afterMark.substr( 0, afterMark.find_first_not_of( "0123456789" ) ), bonesInNameValue ), rig._stem.c_str() );
        const uint32 bonesInName = static_cast<uint32>( bonesInNameValue );
        SW_EXPECT_TRUE( rig._import._bSkinned == SW_TRUE );
        SW_EXPECT_EQUAL( bonesInName, rig._skeleton->getBoneCount() );
        SW_EXPECT_EQUAL( bonesInName, rig._import._mesh._skinBoneCount );
        const uint32 morphCount = static_cast<uint32>( rig._import._mesh._listMorphTarget.size() );
        SW_EXPECT_TRUE( morphCount > 0 );
        SW_ASSERT_TRUE( rig._import._listClip.empty() == false );
        for ( const sw::editor::ModelImportClip& imported : rig._import._listClip )
        {
            SW_EXPECT_EQUAL( bonesInName, imported._clip.getTrackCount() );
            SW_EXPECT_EQUAL( morphCount, static_cast<uint32>( imported._clip.getCurves().size() ) );
            SW_EXPECT_NOT_NULL( imported._clip.findCurve( sw::hashed_string( "morph_00" ) ) );
            SW_EXPECT_TRUE( imported._clip.getDuration() > 0.5f );
        }

        sw::GameObjectManager manager;
        sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Body" ) );
        SW_ASSERT_NOT_NULL( pObject );
        sw::SkeletalMeshComponent* pUnit = pObject->addComponent<sw::SkeletalMeshComponent>();
        SW_ASSERT_NOT_NULL( pUnit );
        pUnit->setSkeleton( rig._skeleton );
        pUnit->setMesh( rig._mesh );
        pUnit->resolveRenderAssets();
        sw::PoseModifierComponent* pModifier = pObject->addComponent<sw::PoseModifierComponent>();
        SW_ASSERT_NOT_NULL( pModifier );
        pModifier->setRigAsset( rig._rig );
        pModifier->dispatchBeginPlay();
        SW_EXPECT_TRUE_MSG( pModifier->isRigReady(), rig._stem.c_str() );
    }
}
