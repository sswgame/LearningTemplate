#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Animation/SpriteClipAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Utility/Xml/TileMapXml.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 도구 애셋 저장 커맨드 — **실패를 소리 내어 말하는가.**
//
// 이 커맨드들의 반환값을 호출부(패널)가 자주 버린다. 그래서 실패가 조용하면 사용자에게는
// 아무 일도 없었던 것처럼 보이고, 실제로 그 조합이 편집을 잃게 만들었다(2026-09-18).
// 다섯 커맨드가 실패를 알리는 방식이 제각각이었던 것도 같이 맞췄다:
//   · saveAnimationGraph · saveDialogueGraph — 성공에만 로그, 실패 둘은 침묵
//   · saveSpriteClip — 성공만 말함
//   · saveSequence · saveTileMap — 로그가 아예 없음

namespace
{
    /** @brief 스코프 동안 남은 Error 로그를 모읍니다. */
    class ScopedErrorLogCollector
    {
    public:
        ScopedErrorLogCollector()
        {
            _handle = Logger::addGlobalListener( SW_DELEGATE_LAMBDA( LogWrittenDelegate, [this]( const LogEntry& entry )
            {
                if ( entry._level == LogLevel::Error )
                    _errorCount++;
            } ) );
        }

        ~ScopedErrorLogCollector() { Logger::removeGlobalListener( _handle ); }

        ScopedErrorLogCollector( const ScopedErrorLogCollector& )            = delete;
        ScopedErrorLogCollector& operator=( const ScopedErrorLogCollector& ) = delete;

        int32 getErrorCount() const { return _errorCount; }

    private:
        DelegateHandle _handle{};
        int32          _errorCount{ 0 };
    };

    /** @brief 어떤 방법으로도 쓸 수 없는 경로 — 디렉터리 이름으로 파일을 만들 수는 없다. */
    string makeUnwritablePath()
    {
        return test::makeTempDirectory( "sw_test_unwritable_dir" ); // 이 경로에 파일을 쓰려 하면 실패한다(이미 디렉터리다).
    }
} // namespace

/**
 * @brief [EditorToolAssetCommandsTest] 애니메이션 그래프 저장 실패는 에러 로그를 남긴다
 * @details 예전에는 성공에만 `SW_LOG_INFO( "Saved …" )` 가 있고 실패 두 경로는 **로그 없이**
 *          `false` 만 돌려줬다. 그리고 `AnimationGraphPanel` 이 그 값을 버린 채 dirty 까지
 *          지웠으므로, 저장이 실패해도 사용자에게는 아무 신호가 없었다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, AnimationGraphSaveFailureIsReported )
{
    const string unwritable = makeUnwritablePath();

    AnimationGraphAsset asset;

    ScopedErrorLogCollector collector;
    SW_EXPECT_FALSE( EditorToolAssetCommands::saveAnimationGraph( asset, unwritable ) );
    SW_EXPECT_TRUE( collector.getErrorCount() > 0 );
}

/**
 * @brief [EditorToolAssetCommandsTest] 대화 그래프 저장 실패도 같은 모양으로 말한다
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, DialogueGraphSaveFailureIsReported )
{
    const string unwritable = makeUnwritablePath();

    DialogueGraphAsset asset;

    ScopedErrorLogCollector collector;
    SW_EXPECT_FALSE( EditorToolAssetCommands::saveDialogueGraph( asset, unwritable ) );
    SW_EXPECT_TRUE( collector.getErrorCount() > 0 );
}

/**
 * @brief [EditorToolAssetCommandsTest] 저장에 성공하면 에러를 남기지 않는다
 * @details 위 둘이 "실패하면 운다" 만 보면 **항상 우는 구현도 통과한다.** 반대 방향을 같이 못 박는다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, SuccessfulSaveIsQuiet )
{
    const string path = test::makeTempPath( "sw_test_animgraph_ok.animgraph.json" );

    AnimationGraphAsset asset;

    ScopedErrorLogCollector collector;
    const bool              bSaved = EditorToolAssetCommands::saveAnimationGraph( asset, path );
    SW_EXPECT_TRUE( bSaved );
    SW_EXPECT_EQUAL( 0, collector.getErrorCount() );
}

/**
 * @brief [EditorToolAssetCommandsTest] 그래프 문서를 읽으면 "없음(새 문서)" · "읽음" · "있는데 읽지 못함" 을 가른다
 * @details 예전에는 셋 다 bool 하나였다. 패널은 없는 파일과 깨진 파일을 가를 수 없어 둘 다 앞 문서의 그래프를 든 채 저장할 수 있게
 *          두었다 — 깨진(또는 더 새 형식의) 파일을 앞 문서로 덮는 길이었다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, GraphLoadTellsMissingFromMalformed )
{
    const string folder      = test::makeTempDirectory( "sw_graph_load" );
    const string missingPath = FileUtil::joinPath( folder, "never_written.animgraph.json" );
    const string brokenPath  = FileUtil::joinPath( folder, "broken.animgraph.json" );
    const string goodPath    = FileUtil::joinPath( folder, "good.animgraph.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( brokenPath, "{ this is not json" ) );

    AnimationGraphAsset good;
    SW_ASSERT_TRUE( EditorToolAssetCommands::saveAnimationGraph( good, goodPath ) );

    AnimationGraphAsset data;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimationGraph( data, missingPath ) == ToolAssetLoadResult::Missing );
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimationGraph( data, goodPath ) == ToolAssetLoadResult::Loaded );
    {
        test::ScopedDefensiveTestLog expected( "a graph file that is not JSON" );
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimationGraph( data, brokenPath ) == ToolAssetLoadResult::Malformed );

        DialogueGraphAsset dialogue;
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadDialogueGraph( dialogue, brokenPath ) == ToolAssetLoadResult::Malformed );
    }
}

/**
 * @brief [EditorToolAssetCommandsTest] 타일맵 · 스프라이트 클립 · 시퀀스도 그래프와 같은 세 갈래로 답한다
 * @details 다섯 로더 가운데 셋이 bool 이라 "없음(새 문서)" 과 "있는데 읽지 못함" 이 섞였고, 타일맵 · 스프라이트 클립 패널은 그 bool 마저 버린 채
 *          늘 "읽었다" 로 표시했다 — 깨진 파일을 앞 문서의 내용으로 덮는 길이었다. 이제 다섯이 같은 결과 타입이고, 문서 패널 기반이 그 결과로
 *          저장을 막는다(`EditorDocumentPanel::reloadDocument`).
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, EveryToolAssetLoadTellsMissingFromMalformed )
{
    const string folder = test::makeTempDirectory( "sw_tool_load" );
    const string broken = FileUtil::joinPath( folder, "broken.txt" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( broken, "<<< merge conflict { not a document" ) );
    const string missing = FileUtil::joinPath( folder, "never_written.txt" );

    string         status;
    TileMapXmlData tileMap;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadTileMap( missing, tileMap, status ) == ToolAssetLoadResult::Missing );
    SpriteClipAsset clip;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadSpriteClip( clip, status, missing ) == ToolAssetLoadResult::Missing );
    SequenceAsset sequence;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadSequence( sequence, missing ) == ToolAssetLoadResult::Missing );
    {
        test::ScopedDefensiveTestLog expected( "tool documents that cannot be read" );
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadTileMap( broken, tileMap, status ) == ToolAssetLoadResult::Malformed );
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadSpriteClip( clip, status, broken ) == ToolAssetLoadResult::Malformed );
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadSequence( sequence, broken ) == ToolAssetLoadResult::Malformed );
    }
}

/**
 * @brief [EditorToolAssetCommandsTest] 프리팹에 적용하기는 프리팹 경로의 형식으로 쓰고, 프리팹이 아닌 경로는 덮지 않는다
 * @details "Apply to Prefab" 이 `.prefab.json` 에 XML 을 써서 그 프리팹이 다시는 읽히지 않았다. "Apply Overrides" 는 포커스된 에셋 경로(콘텐츠
 *          브라우저에서 마지막에 클릭한 씬 · 머티리얼)를 먼저 써서 그 파일을 프리팹으로 덮었다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, ApplyToPrefabWritesThePrefabsFormatAndNothingElse )
{
    GameObjectManager manager;
    GameObject*       pObj = manager.createGameObject( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pObj->addComponent<SceneComponent>() );

    const string jsonPath = test::makeTempPath( "crate.prefab.json" );
    SW_ASSERT_TRUE( EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, jsonPath ) );
    PrefabAsset reloaded;
    SW_EXPECT_TRUE( reloaded.loadFromJsonFile( jsonPath ) );

    const string scenePath = test::makeTempPath( "level.scene.xml" );
    const string kScene    = "<Scene name=\"Level\"/>\n";
    SW_ASSERT_TRUE( FileUtil::writeTextFile( scenePath, kScene ) );
    {
        test::ScopedDefensiveTestLog expected( "a scene path is not a prefab path" );
        SW_EXPECT_FALSE( EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, scenePath ) );
    }
    string sceneAfter;
    SW_ASSERT_TRUE( FileUtil::readTextFile( scenePath, sceneAfter ) );
    SW_EXPECT_STREQ( kScene.c_str(), sceneAfter.c_str() );
}

/**
 * @brief [EditorToolAssetCommandsTest] 오버라이드는 컴포넌트를 안정 키로 짝짓고, 되돌리기는 그 컴포넌트의 그 비트만 바꾼다
 * @details 예전에는 컴포넌트를 타입 이름으로 짝지어 같은 타입의 둘째 컴포넌트(소켓)가 첫째의 원형과 비교됐고(바꾸지 않은 위치가 오버라이드로
 *          보였다), 되돌리기는 첫째에 적용됐다. 비트필드는 바이트째 견주고 옮겨, 같은 바이트의 런타임 플래그(재생 중)가 다르면 반복 설정이
 *          오버라이드로 보였고, 되돌리면 재생이 멈췄다. 되돌린 값은 알리지 않아 위치가 월드 행렬에 들지 않았다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, OverridesPairComponentsByKeyAndRevertOnlyTheirOwnValue )
{
    GameObjectManager manager;
    auto              makeObject = [&manager]( const utf8* pName )
    {
        GameObject* pObj = manager.createGameObject( hashed_string( pName ) );
        (void)pObj->addComponent<SceneComponent>();
        SceneComponent* pSocket = pObj->addComponent<SceneComponent>();
        pSocket->setLocalPosition( float3( 5.0f, 0.0f, 0.0f ) );
        SpriteAnimatorComponent* pAnimator = pObj->addComponent<SpriteAnimatorComponent>();
        pAnimator->play( "Walk", true );
        return pObj;
    };
    GameObject* pCdo      = makeObject( "Template" );
    GameObject* pInstance = makeObject( "Instance" );
    pCdo->getComponent<SpriteAnimatorComponent>()->stop(); // 런타임 비트(재생 중)만 다르다
    manager.flushSceneTransforms();

    auto countModified = []( const vector<PrefabOverrideItem>& listItem )
    {
        uint32 count = 0;
        for ( const PrefabOverrideItem& item : listItem )
            count += item._bModified ? 1 : 0;
        return count;
    };
    vector<PrefabOverrideItem> listOverride;
    EditorToolAssetCommands::collectComponentOverrides( pInstance, pCdo, listOverride );
    SW_EXPECT_EQUAL( 0u, countModified( listOverride ) );

    // 소켓을 옮기고 반복을 끈다 — 그 둘만 오버라이드다.
    SceneComponent* pInstanceSocket = static_cast<SceneComponent*>( ComponentStableKey::findComponent( pInstance, "SceneComponent#1" ) );
    SW_ASSERT_NOT_NULL( pInstanceSocket );
    pInstanceSocket->setLocalPosition( float3( 7.0f, 0.0f, 0.0f ) );
    SpriteAnimatorComponent* pInstanceAnimator = pInstance->getComponent<SpriteAnimatorComponent>();
    pInstanceAnimator->setRepeat( false );
    manager.flushSceneTransforms();
    listOverride.clear();
    EditorToolAssetCommands::collectComponentOverrides( pInstance, pCdo, listOverride );
    SW_EXPECT_EQUAL( 2u, countModified( listOverride ) );

    for ( PrefabOverrideItem& item : listOverride )
    {
        if ( item._bModified == false )
            continue;
        SW_EXPECT_TRUE( EditorToolAssetCommands::revertComponentOverride( pInstance, pCdo, item ) );
        SW_EXPECT_FALSE( item._bModified );
    }
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( 5.0f, pInstanceSocket->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pInstanceSocket->getWorldPosition()._x, 1e-4f ); // 알렸다
    SW_EXPECT_TRUE( pInstanceAnimator->isRepeating() );
    SW_EXPECT_TRUE( pInstanceAnimator->isPlaying() ); // 같은 바이트의 다른 비트는 그대로다
}
