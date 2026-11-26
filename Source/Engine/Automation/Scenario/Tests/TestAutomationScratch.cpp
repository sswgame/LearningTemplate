#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

// AutomationScratchTest — 작업 도메인(`scratch/`)이 이 프로세스의 실행 뿌리 아래로 풀린다. 나란히 도는 실행(백엔드마다 하나)의 사본이 이 프로세스의 리소스 트리에 없는 근거다(nogpu).

/**
 * @brief [AutomationScratchTest] 사본 id 는 적은 그대로 실행 뿌리 아래 디스크 자리로 풀리고, 그 자리는 다시 같은 id 로 돌아온다. 리소스 트리(`Content/`) 안이 아니다
 * @details 변이: 작업 도메인을 리소스 루트 아래(예전 `Content/scratch/`)로 풀면 둘째 · 셋째 단언이 진다 — 그 자리는 같은 체크아웃의 모든 App 이 나눠 썼다.
 */
SW_TEST_CASE( AutomationScratchTest, ScratchDomainLivesUnderTheRunRoot )
{
    const sw::AssetPaths&            paths = sw::engine::getSubsystem<sw::AssetPaths>();
    const sw::optional<sw::DiskPath> file  = paths.toDiskPath( "scratch/assetmanage/probe.material" );
    SW_REQUIRE_TRUE( file.has_value() );
    SW_EXPECT_TRUE( sw::FileUtil::isPathUnderRoot( sw::ProcessRoots::get()._run.str(), file->str() ) );
    SW_EXPECT_FALSE( sw::FileUtil::isPathAtOrUnderRoot( sw::ProcessRoots::get()._content.str(), file->str() ) );
    SW_EXPECT_EQUAL( sw::string( "scratch/assetmanage/probe.material" ), paths.toAssetID( *file ) );
    // 가져오기 그릇 아래 토막 이름도 그대로다(예전 pid 꼬리는 `scratch/import/<이름>_p<pid>` 였다)
    const sw::optional<sw::DiskPath> imported = paths.toDiskPath( "scratch/import/editor.flowimport/a.prefab.xml" );
    SW_REQUIRE_TRUE( imported.has_value() );
    SW_EXPECT_EQUAL( sw::string( "scratch/import/editor.flowimport/a.prefab.xml" ), paths.toAssetID( *imported ) );
}

/**
 * @brief [AutomationScratchTest] 예전 자리(`Content/scratch/`)에 남은 찌꺼기는 리소스 id 가 아니다 — 같은 id 의 실행 뿌리 사본과 섞이지 않는다
 * @details 변이: `toAssetID` 가 리소스 루트 아래 `scratch/` 를 id 로 돌려주면 에디터 감시가 찌꺼기 사건을 이 실행의 사본 사건으로 읽는다.
 */
SW_TEST_CASE( AutomationScratchTest, LeftoversUnderTheContentTreeAreNotScratchIDs )
{
    const sw::AssetPaths&  paths    = sw::engine::getSubsystem<sw::AssetPaths>();
    const sw::AbsolutePath leftover = paths.getRootFolderPath().join( sw::AssetPaths::kScratchDomain ).join( "assetmanage_p12/probe.material" );
    SW_EXPECT_TRUE( paths.toAssetID( leftover ).empty() );
}
