#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionDocWriter.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 리플렉션 → Markdown API 문서(`App --write-reflection-docs`).

/**
 * @brief [ReflectionDocWriterTest] 등록된 타입으로 모듈별 문서와 목록을 쓴다 — 프로퍼티 · 함수 인자 · 이벤트 · 역할 플래그 · 열거형, 같은 등록이면 같은 바이트
 */
SW_TEST_CASE( ReflectionDocWriterTest, WritesModulePagesAndIndexDeterministically )
{
    const sw::string firstDir  = test::makeTempPath( "docs_first" );
    const sw::string secondDir = test::makeTempPath( "docs_second" );
    const uint32     fileCount = sw::ReflectionDocWriter::writeMarkdown( sw::engine::getTypeRegistry(), firstDir );
    SW_ASSERT_TRUE( fileCount >= 2 );
    SW_EXPECT_EQUAL( fileCount, sw::ReflectionDocWriter::writeMarkdown( sw::engine::getTypeRegistry(), secondDir ) );

    sw::string index;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( firstDir, "index.md" ), index ) );
    SW_EXPECT_TRUE_MSG( index.find( "[`sw::InvokeDemoActor`](Engine.md#sw-invokedemoactor)" ) != sw::string::npos, index.c_str() );

    const sw::TypeInfo* pType = sw::InvokeDemoActor::StaticType();
    SW_ASSERT_NOT_NULL( pType );
    const sw::string pageName = sw::ReflectionDocWriter::makeModuleFileName( pType->_moduleName );
    sw::string       page;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( firstDir, pageName ), page ) );
    SW_EXPECT_TRUE( page.find( "## sw::InvokeDemoActor" ) != sw::string::npos );
    SW_EXPECT_TRUE( page.find( "- `int32 heal(int32 amount, float32 multiplier = 1.5f)`" ) != sw::string::npos );
    SW_EXPECT_TRUE( page.find( "- `_onHpChanged(int32 newHp, string reason)`" ) != sw::string::npos );
    SW_EXPECT_TRUE( page.find( "| `_health` | `int32` |" ) != sw::string::npos );
    SW_EXPECT_TRUE( page.find( "RepNotify=onHealthReplicated" ) != sw::string::npos );
    SW_EXPECT_TRUE( page.find( "## sw::SampleStatus" ) != sw::string::npos );
    SW_EXPECT_TRUE( page.find( "| `Attacking` | 2 |" ) != sw::string::npos );

    sw::string secondPage;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( secondDir, pageName ), secondPage ) );
    SW_EXPECT_TRUE( page == secondPage );
}
