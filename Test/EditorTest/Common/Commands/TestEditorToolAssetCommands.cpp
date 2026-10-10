#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Engine/Animation/Graph/AnimGraphAsset.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/TileMap/TileMapXML.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 도구 애셋 저장 커맨드 — **실패를 소리 내어 말하는가.**
//
// 이 커맨드들의 반환값을 호출부(패널)가 자주 버린다. 그래서 실패가 조용하면 사용자에게는
// 아무 일도 없었던 것처럼 보이고 편집을 잃는다. 다섯 커맨드는 실패를 같은 모양으로 알린다.

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

    /** @brief 스코프 동안 도구 애셋 커맨드가 남긴 Error · Warning 로그의 문구를 모읍니다. */
    class ScopedLogMessageCollector
    {
    public:
        ScopedLogMessageCollector()
        {
            _handle = Logger::addGlobalListener( SW_DELEGATE_LAMBDA( LogWrittenDelegate, [this]( const LogEntry& entry )
            {
                // 커맨드가 남긴 것만 본다 — 파일 계층의 오류 · 시험 프레임워크의 실패 보고는 모양을 비교할 대상이 아니다.
                if ( entry._caller != "EditorToolAssetCommands" )
                    return;
                if ( entry._level == LogLevel::Error )
                    _listError.push_back( entry._message );
                else if ( entry._level == LogLevel::Warning )
                    _listWarning.push_back( entry._message );
            } ) );
        }

        ~ScopedLogMessageCollector() { Logger::removeGlobalListener( _handle ); }

        ScopedLogMessageCollector( const ScopedLogMessageCollector& )            = delete;
        ScopedLogMessageCollector& operator=( const ScopedLogMessageCollector& ) = delete;

        const vector<string>& getErrors() const { return _listError; }

        /** @brief @p text 가 든 경고가 있는지입니다(방어 시험 범위에서는 문구 앞에 표식이 붙는다). */
        bool hasWarningContaining( const string& text ) const
        {
            for ( const string& message : _listWarning )
            {
                if ( message.find( text ) != string::npos )
                    return true;
            }
            return false;
        }

    private:
        DelegateHandle _handle{};
        vector<string> _listError;
        vector<string> _listWarning;
    };

    /** @brief 어떤 방법으로도 쓸 수 없는 경로 — 디렉터리 이름으로 파일을 만들 수는 없다. */
    string makeUnwritablePath()
    {
        return test::makeTempDirectory( "sw_test_unwritable_dir" ); // 이 경로에 파일을 쓰려 하면 실패한다(이미 디렉터리다).
    }
} // namespace

/**
 * @brief [EditorToolAssetCommandsTest] 애니메이션 그래프 저장 실패는 에러 로그를 남긴다
 * @details 실패 경로가 **로그 없이** `false` 만 돌려주면, 패널이 그 값을 버리는 순간 저장이 실패해도
 *          사용자에게는 아무 신호가 없다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, AnimGraphSaveFailureIsReported )
{
    const string unwritable = makeUnwritablePath();

    AnimGraphAsset asset;

    ScopedErrorLogCollector collector;
    SW_EXPECT_FALSE( EditorToolAssetCommands::saveAnimGraph( asset, unwritable ) );
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

    AnimGraphAsset asset;

    ScopedErrorLogCollector collector;
    const bool              bSaved = EditorToolAssetCommands::saveAnimGraph( asset, path );
    SW_EXPECT_TRUE( bSaved );
    SW_EXPECT_EQUAL( 0, collector.getErrorCount() );
}

/**
 * @brief [EditorToolAssetCommandsTest] 그래프 문서를 읽으면 "없음(새 문서)" · "읽음" · "있는데 읽지 못함" 을 가른다
 * @details 셋을 bool 하나로 답하면 패널이 없는 파일과 깨진 파일을 가를 수 없어, 둘 다 앞 문서의 그래프를 든 채 저장할 수 있게
 *          둔다 — 깨진(또는 더 새 형식의) 파일을 앞 문서로 덮는다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, GraphLoadTellsMissingFromMalformed )
{
    const string folder      = test::makeTempDirectory( "sw_graph_load" );
    const string missingPath = FileUtil::joinPath( folder, "never_written.animgraph.json" );
    const string brokenPath  = FileUtil::joinPath( folder, "broken.animgraph.json" );
    const string goodPath    = FileUtil::joinPath( folder, "good.animgraph.json" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( brokenPath, "{ this is not json" ) );

    AnimGraphAsset good;
    SW_ASSERT_TRUE( EditorToolAssetCommands::saveAnimGraph( good, goodPath ) );

    AnimGraphAsset data;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimGraph( data, missingPath ) == ToolAssetLoadResult::Missing );
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimGraph( data, goodPath ) == ToolAssetLoadResult::Loaded );
    {
        test::ScopedDefensiveTestLog expected( "a graph file that is not JSON" );
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimGraph( data, brokenPath ) == ToolAssetLoadResult::Malformed );

        DialogueGraphAsset dialogue;
        SW_EXPECT_TRUE( EditorToolAssetCommands::loadDialogueGraph( dialogue, brokenPath ) == ToolAssetLoadResult::Malformed );
    }
}

/**
 * @brief [EditorToolAssetCommandsTest] 타일맵 · 스프라이트 클립 · 시퀀스도 그래프와 같은 세 갈래로 답한다
 * @details 다섯 로더가 같은 결과 타입이고, 문서 패널 기반이 그 결과로 저장을 막는다(`EditorDocumentPanel::reloadDocument`). bool 로 답하는
 *          로더는 "없음(새 문서)" 과 "있는데 읽지 못함" 을 섞어, 깨진 파일을 앞 문서의 내용으로 덮게 한다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, EveryToolAssetLoadTellsMissingFromMalformed )
{
    const string folder = test::makeTempDirectory( "sw_tool_load" );
    const string broken = FileUtil::joinPath( folder, "broken.txt" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( broken, "<<< merge conflict { not a document" ) );
    const string missing = FileUtil::joinPath( folder, "never_written.txt" );

    string         status;
    TileMapXMLData tileMap;
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
 * @details "Apply to Prefab" 이 `.prefab.json` 에 XML 을 쓰면 그 프리팹이 다시는 읽히지 않는다. "Apply Overrides" 가 포커스된 에셋 경로(콘텐츠
 *          브라우저에서 마지막에 클릭한 씬 · 머티리얼)를 먼저 쓰면 그 파일을 프리팹으로 덮는다.
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
 * @details 컴포넌트를 타입 이름으로 짝지으면 같은 타입의 둘째 컴포넌트(소켓)가 첫째의 원형과 비교되어 바꾸지 않은 위치가 오버라이드로 보이고,
 *          되돌리기가 첫째에 적용된다. 비트필드를 바이트째 견주고 옮기면 같은 바이트의 런타임 플래그(재생 중)가 다를 때 반복 설정이
 *          오버라이드로 보이고, 되돌리면 재생이 멈춘다. 되돌린 값을 알리지 않으면 위치가 월드 행렬에 들지 않는다.
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
        {
            count += item._bModified ? 1 : 0;
        }
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

/**
 * @brief [EditorToolAssetCommandsTest] 덮어쓴 것만 저장한 씬을 다시 연 인스턴스에서도 오버라이드 목록 · 되돌리기가 그대로 동작하고, 되돌린 값은 다음 저장에서 빠진다
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, RevertingAnOverrideRemovesItFromTheSavedScene )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const string prefabPath = test::makeTempPath( "revert_crate.prefab.xml" );
    {
        GameObjectManager authoring;
        GameObject*       pSource = authoring.createGameObject( hashed_string( "Crate" ) );
        pSource->addComponent<SceneComponent>()->setLocalPosition( float3( 1.0f, 2.0f, 3.0f ) );
        PrefabAsset prefab;
        prefab.setFromGameObject( pSource );
        SW_ASSERT_TRUE( prefab.saveToXMLFile( prefabPath ) );
        SW_ASSERT_TRUE( prefab.saveToBinaryFile( AssetCookPath::toCookedPath( prefabPath ) ) ); // 배포본은 쿠킹본만 읽는다
        engine::getAssetManager().getPrefabCache().reload( prefabPath );
    }

    SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    SceneDocument                  authored;
    SceneDocument::SceneObjectNode entity;
    entity._name   = "Crate";
    entity._prefab = prefabPath;
    authored._listSceneObjectNode.push_back( entity );
    Scene* pPlaced = manager.createScene( "RevertWorld" );
    SW_ASSERT_TRUE( pPlaced->instantiate( authored ) );
    GameObject* pPlacedCrate = pPlaced->getObjectManager()->findGameObjectByName( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pPlacedCrate );
    pPlacedCrate->getPrimarySceneComponent()->setLocalPosition( float3( 4.0f, 4.0f, 4.0f ) );
    SceneDocument saved;
    SW_ASSERT_TRUE( pPlaced->serializeToDocument( saved ) );
    SW_ASSERT_TRUE( saved._listSceneObjectNode.size() == 1 && saved._listSceneObjectNode[0]._prefabOverrideXML.empty() == false );

    // 다시 연 인스턴스 — 덮어쓴 위치가 원형 위에 얹혀 있다.
    Scene* pReopened = manager.createScene( "RevertWorldReopened" );
    SW_ASSERT_TRUE( pReopened->instantiate( saved ) );
    GameObject* pInstance = pReopened->getObjectManager()->findGameObjectByName( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pInstance );
    GameObjectManager scratch;
    GameObject*       pCdo    = scratch.createGameObject( hashed_string( "Cdo" ) );
    PrefabAsset*      pPrefab = engine::getAssetManager().getPrefabCache().loadPrefab( prefabPath );
    SW_ASSERT_TRUE( pPrefab != nullptr && pPrefab->applyStateTo( pCdo ) );

    vector<PrefabOverrideItem> listOverride;
    EditorToolAssetCommands::collectComponentOverrides( pInstance, pCdo, listOverride );
    uint32 revertedCount = 0;
    for ( PrefabOverrideItem& item : listOverride )
    {
        if ( item._bModified == false )
            continue;
        SW_EXPECT_STREQ( "_localPosition", item._propertyName.c_str() );
        SW_EXPECT_TRUE( EditorToolAssetCommands::revertComponentOverride( pInstance, pCdo, item ) );
        ++revertedCount;
    }
    SW_EXPECT_EQUAL( 1u, revertedCount );
    SW_EXPECT_TRUE( pInstance->getPrimarySceneComponent()->getLocalPosition() == float3( 1.0f, 2.0f, 3.0f ) );

    SceneDocument resaved;
    SW_ASSERT_TRUE( pReopened->serializeToDocument( resaved ) );
    SW_ASSERT_EQUAL( size_t( 1 ), resaved._listSceneObjectNode.size() );
    SW_EXPECT_TRUE_MSG( resaved._listSceneObjectNode[0]._prefabOverrideXML.empty(), resaved._listSceneObjectNode[0]._prefabOverrideXML.c_str() );
    SW_EXPECT_TRUE( resaved._listSceneObjectNode[0]._embeddedXML.empty() );
    manager.shutdown();
}

/**
 * @brief [EditorToolAssetCommandsTest] 다섯 도구 문서의 저장 실패는 같은 모양의 오류 한 줄이다
 * @details `saveToolDocument` 한 벌이 알리므로 종류 이름만 다르고 모양은 같다: `Failed to save the <종류> to '<경로>'`.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, EverySaveFailureHasTheSameShape )
{
    const string unwritable = makeUnwritablePath();

    struct SaveCase
    {
        const utf8* _pLabel;
        bool        _bSaved;
        size_t      _errorCount;
        string      _firstError;
    };
    vector<SaveCase> listCase;
    const auto       runCase = [&listCase]( const utf8* pLabel, auto&& save )
    {
        ScopedLogMessageCollector collector;
        const bool                bSaved = save();
        listCase.push_back( SaveCase{ pLabel, bSaved, collector.getErrors().size(), collector.getErrors().empty() ? string{} : collector.getErrors()[0] } );
    };
    runCase( "animation graph", [&]()
    { return EditorToolAssetCommands::saveAnimGraph( AnimGraphAsset{}, unwritable ); } );
    runCase( "dialogue graph", [&]()
    { return EditorToolAssetCommands::saveDialogueGraph( DialogueGraphAsset{}, unwritable ); } );
    runCase( "tile map", [&]()
    { return EditorToolAssetCommands::saveTileMap( unwritable, TileMapXMLData{} ); } );
    runCase( "sprite clip", [&]()
    { return EditorToolAssetCommands::saveSpriteClip( SpriteClipAsset{}, unwritable ); } );
    runCase( "sequence", [&]()
    { return EditorToolAssetCommands::saveSequence( SequenceAsset{}, unwritable ); } );

    for ( const SaveCase& saveCase : listCase )
    {
        SW_EXPECT_FALSE( saveCase._bSaved );
        SW_EXPECT_TRUE_MSG( saveCase._errorCount == 1, saveCase._pLabel );
        const string expectedPrefix = string( "Failed to save the " ) + saveCase._pLabel + " to '";
        SW_EXPECT_TRUE_MSG( StringUtil::startsWith( saveCase._firstError, expectedPrefix ), saveCase._firstError.c_str() );
    }

    // 경로도 기본 문서도 없으면 같은 문구로 경로가 없다고 말한다.
    ScopedLogMessageCollector collector;
    SW_EXPECT_FALSE( EditorToolAssetCommands::saveSequence( SequenceAsset{}, {} ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), collector.getErrors().size() );
    SW_EXPECT_TRUE( StringUtil::startsWith( collector.getErrors()[0], "Failed to save the sequence - no file path" ) );
}

/**
 * @brief [EditorToolAssetCommandsTest] 다섯 도구 문서는 읽지 못한 파일을 같은 경고로 알리고, 상태 문구도 같은 모양이다
 * @details 상태 문구가 있는 둘(타일맵 · 스프라이트 클립)은 `No file yet: ` · `Failed to read ` · `Loaded ` 로 같은 모양이다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, EveryUnreadableDocumentIsReportedTheSameWay )
{
    const string folder  = test::makeTempDirectory( "sw_tool_unreadable" );
    const string broken  = FileUtil::joinPath( folder, "broken.txt" );
    const string missing = FileUtil::joinPath( folder, "never_written.txt" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( broken, "<<< merge conflict { not a document" ) );

    test::ScopedDefensiveTestLog expected( "tool documents that cannot be read" );
    ScopedLogMessageCollector    collector;

    AnimGraphAsset animGraph;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadAnimGraph( animGraph, broken ) == ToolAssetLoadResult::Malformed );
    DialogueGraphAsset dialogueGraph;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadDialogueGraph( dialogueGraph, broken ) == ToolAssetLoadResult::Malformed );
    string         tileStatus;
    TileMapXMLData tileMap;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadTileMap( broken, tileMap, tileStatus ) == ToolAssetLoadResult::Malformed );
    SW_EXPECT_TRUE( StringUtil::startsWith( tileStatus, "Failed to read " ) );
    string          clipStatus;
    SpriteClipAsset clip;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadSpriteClip( clip, clipStatus, broken ) == ToolAssetLoadResult::Malformed );
    SW_EXPECT_TRUE( StringUtil::startsWith( clipStatus, "Failed to read " ) );
    SequenceAsset sequence;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadSequence( sequence, broken ) == ToolAssetLoadResult::Malformed );

    const utf8* const arrLabel[] = { "animation graph", "dialogue graph", "tile map", "sprite clip", "sequence" };
    for ( const utf8* pLabel : arrLabel )
    {
        SW_EXPECT_TRUE_MSG( collector.hasWarningContaining( string( "Could not read the " ) + pLabel + " '" ), pLabel );
    }

    SW_EXPECT_TRUE( EditorToolAssetCommands::loadTileMap( missing, tileMap, tileStatus ) == ToolAssetLoadResult::Missing );
    SW_EXPECT_STREQ( ( "No file yet: " + missing ).c_str(), tileStatus.c_str() );
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadSpriteClip( clip, clipStatus, missing ) == ToolAssetLoadResult::Missing );
    SW_EXPECT_STREQ( ( "No file yet: " + missing ).c_str(), clipStatus.c_str() );
}

/**
 * @brief [EditorToolAssetCommandsTest] 스프라이트 클립에 이미지를 주면 문서로 읽지 않고 아틀라스로 쓴다 — 파일이 있어도
 * @details 아틀라스 폴백을 "파일이 없을 때" 안에 두면 실제로 있는 이미지를 클립 문서로 읽으려다 `Malformed` 가 된다.
 */
SW_TEST_CASE( EditorToolAssetCommandsTest, SpriteClipTakesAnExistingImageAsTheAtlas )
{
    const string image = test::makeTempPath( "sw_tool_atlas.png" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( image, "not really a png - never decoded here" ) );

    string          status;
    SpriteClipAsset clip;
    SW_EXPECT_TRUE( EditorToolAssetCommands::loadSpriteClip( clip, status, image ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_STREQ( image.c_str(), clip._atlasPath.c_str() );
    SW_EXPECT_STREQ( "Atlas from focused texture", status.c_str() );
}
