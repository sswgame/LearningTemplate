#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"

#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"

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
        const string dir = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_test_unwritable_dir" );
        FileUtil::ensureDirectoryExists( dir );
        return dir; // 이 경로에 파일을 쓰려 하면 실패한다(이미 디렉터리다).
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
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( Delegate<void()>, [unwritable]()
    {
        FileUtil::removeDirectory( unwritable );
    } ) );

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
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( Delegate<void()>, [unwritable]()
    {
        FileUtil::removeDirectory( unwritable );
    } ) );

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
    const string path = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_test_animgraph_ok.animgraph.json" );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( Delegate<void()>, [path]()
    {
        FileUtil::removeFile( path );
    } ) );

    AnimationGraphAsset asset;

    ScopedErrorLogCollector collector;
    const bool              bSaved = EditorToolAssetCommands::saveAnimationGraph( asset, path );
    SW_EXPECT_TRUE( bSaved );
    SW_EXPECT_EQUAL( 0, collector.getErrorCount() );
}
