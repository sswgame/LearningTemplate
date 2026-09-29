#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 전역 변수 프리셋 — **테스트용 변수는 저장하지도 적용하지도 않는다.**
//
// 테스트용(`SW_TEST_GLOBAL_VARIABLE_*`)은 벤치 · 자동화 스위치다. 프리셋에 `gv_profileFrames` 가 들어가면 불러온 에디터가
// N 프레임 뒤 스스로 꺼지고, `gv_crashTest` 가 들어가면 일부러 죽는다. 실행 한 번에만 줄 값이다.

/**
 * @brief [EditorGlobalVariableCommandsTest] 저장한 프리셋에 테스트용 변수가 들어가지 않는다
 */
SW_TEST_CASE( EditorGlobalVariableCommandsTest, SavedPresetSkipsTestOnlyVariables )
{
    GlobalVariableManager manager;
    int32                 runtimeValue{ 3 };
    int32                 testOnlyValue{ 5 };
    SW_ASSERT_TRUE( manager.registerVariable( "gv_presetRuntime", GlobalVariableType::Int32, &runtimeValue, int32{ 3 }, "" ) );
    SW_ASSERT_TRUE( manager.registerVariable( "gv_presetTestOnly", GlobalVariableType::Int32, &testOnlyValue, int32{ 5 }, "", "", "", 4, true ) );
    const ScopedGlobalVariableManagerService service{ manager };

    const string presetPath = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_gv_saved.gvpreset.xml" );
    SW_ASSERT_TRUE( EditorGlobalVariableCommands::savePreset( presetPath, "probe" ) );

    XmlDocument doc;
    SW_ASSERT_TRUE( doc.loadFile( presetPath ) );
    bool bHasRuntime{ false };
    bool bHasTestOnly{ false };
    for ( XmlNode varNode = doc.getRoot().findChild( "Var" ); varNode.isValid(); varNode = varNode.findNextSibling( "Var" ) )
    {
        const utf8* pName = varNode.findAttribute( "name" );
        if ( pName == nullptr )
            continue;
        bHasRuntime  = bHasRuntime || string_view{ pName } == "gv_presetRuntime";
        bHasTestOnly = bHasTestOnly || string_view{ pName } == "gv_presetTestOnly";
    }
    FileUtil::removeFile( presetPath );

    SW_EXPECT_TRUE( bHasRuntime );
    SW_EXPECT_FALSE( bHasTestOnly );
}

/**
 * @brief [EditorGlobalVariableCommandsTest] 테스트용 값을 담은 옛 프리셋을 불러와도 그 값은 적용하지 않는다
 * @details 필터가 생기기 전에 저장한 프리셋(세션 프리셋 포함)은 테스트용 변수를 담고 있다.
 */
SW_TEST_CASE( EditorGlobalVariableCommandsTest, LoadedPresetSkipsTestOnlyVariables )
{
    GlobalVariableManager manager;
    int32                 runtimeValue{ 3 };
    int32                 testOnlyValue{ 5 };
    SW_ASSERT_TRUE( manager.registerVariable( "gv_presetRuntime", GlobalVariableType::Int32, &runtimeValue, int32{ 3 }, "" ) );
    SW_ASSERT_TRUE( manager.registerVariable( "gv_presetTestOnly", GlobalVariableType::Int32, &testOnlyValue, int32{ 5 }, "", "", "", 4, true ) );
    const ScopedGlobalVariableManagerService service{ manager };

    XmlDocument doc;
    XmlNode     root = doc.appendRoot( "GlobalVariablesPreset" );
    root.appendAttribute( "name", "old" );
    for ( const utf8* pName : { "gv_presetRuntime", "gv_presetTestOnly" } )
    {
        XmlNode varNode = root.appendChild( "Var" );
        varNode.appendAttribute( "name", pName );
        varNode.appendAttribute( "type", "Int32" );
        varNode.appendAttribute( "value", "99" );
    }
    const string presetPath = FileUtil::joinPath( FileUtil::getTempDirectory(), "sw_editor_gv_old.gvpreset.xml" );
    SW_ASSERT_TRUE( doc.saveFile( presetPath ) );

    SW_ASSERT_TRUE( EditorGlobalVariableCommands::loadPreset( presetPath ) );
    FileUtil::removeFile( presetPath );

    SW_EXPECT_EQUAL( 99, runtimeValue );
    SW_EXPECT_EQUAL( 5, testOnlyValue );
}
