#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/GameSelection.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Scene/SceneDocument.h"

using namespace sw;

// 엔진과 활성 게임의 씬 파일(하위 폴더 포함)이 모두 지금 형식으로 읽힌다 — 시나리오가 여는 씬은 시나리오를 돌려야 깨진 줄 알았다(옛 <WorldSettings> 가 남은 씬 다섯).
// 다른 게임의 씬은 그 게임의 키트 컴포넌트를 쓰므로 그 게임이 활성일 때 본다(ScenarioFilesTest 와 같은 범위).
SW_TEST_CASE( ContentScenesTest, EngineAndActiveGameScenesLoad )
{
    const string& contentRoot  = engine::getSubsystem<AssetPaths>().getRootFolderPath();
    const string  listFolder[] = { FileUtil::joinPath( contentRoot, "engine" ),
                                   FileUtil::joinPath( contentRoot, "game/" + StringUtil::toLower( GameSelection::getName().c_str() ) ) };
    uint32        sceneCount   = 0;
    for ( const string& folder : listFolder )
    {
        vector<string> listPath;
        SW_EXPECT_TRUE( FileUtil::collectFiles( folder, ".scene.xml", listPath, true ) );
        for ( const string& path : listPath )
        {
            SceneDocument document;
            const bool    bLoaded = document.loadXMLFromDiskFile( path );
            const string  failure = bLoaded ? string() : string( path + ": could not be loaded (see the log)" );
            SW_EXPECT_EQUAL( string(), failure );
            ++sceneCount;
        }
    }
    SW_EXPECT_TRUE( sceneCount > 0 );
}
