// EditorDocumentAutosaveTest — 문서 패널 자동 저장 사본의 디스크 규칙(감사 E-1): 쓴 사본은 복구 후보로 읽히고 글이 같으며, 정상 저장(remove)하면 사라지고,
// 다른 프로젝트 · 사본 뒤에 바뀐 원본은 거절 이유가 붙는다. 스크래치 폴더만 쓴다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"

namespace
{
    struct EditorDocumentAutosaveTestInternal
    {
        static constexpr uint64 kSourceHash = 0x1234u;

        static uint64 computeKeptSourceHash( const sw::editor::EditorDocumentAutosaveEntry& /*entry*/ ) { return kSourceHash; }
        static uint64 computeEditedSourceHash( const sw::editor::EditorDocumentAutosaveEntry& /*entry*/ ) { return kSourceHash + 1u; }

        static sw::editor::EditorDocumentAutosaveEntry makeEntry( sw::string_view projectRoot )
        {
            sw::editor::EditorDocumentAutosaveEntry entry{};
            entry._documentPath      = "game/playground/materials/probe.material";
            entry._text              = "<Material roughness=\"0.7\"/>";
            entry._projectRoot       = sw::string{ projectRoot };
            entry._sourceContentHash = kSourceHash;
            return entry;
        }
    };
} // namespace

/** @brief [EditorDocumentAutosaveTest] 쓴 사본은 복구 후보로 읽히고 그 글이 같다. 문서를 정상 저장하면(remove) 후보가 사라진다 */
SW_TEST_CASE( EditorDocumentAutosaveTest, WrittenCopyIsRecoverableUntilSaved )
{
    using Internal                             = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath       sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath       projectRoot   = sw::test::makeScratchPath( "Project" );
    const auto                   entry         = Internal::makeEntry( projectRoot.str() );
    const sw::vector<sw::string> listSession{ sessionFolder.str() };
    SW_REQUIRE_TRUE( entry.write( sessionFolder.str() ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._documentPath == entry._documentPath );
    SW_EXPECT_TRUE( listEntry[0]._refusal.empty() );
    sw::string text;
    SW_REQUIRE_TRUE( listEntry[0].readText( text ) );
    SW_EXPECT_TRUE( text == entry._text );

    sw::editor::EditorDocumentAutosaveEntry::remove( sessionFolder.str(), entry._documentPath );
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_EXPECT_EQUAL( size_t{ 0 }, listEntry.size() ); // 저장한 원본이 사본보다 새것이다 — 다음 기동이 묻지 않는다
}

/** @brief [EditorDocumentAutosaveTest] 다른 프로젝트의 사본 · 사본 뒤에 바뀐 원본에는 거절 이유가 붙는다(남의 일을 덮지 않는다) */
SW_TEST_CASE( EditorDocumentAutosaveTest, ForeignProjectAndChangedSourceAreRefused )
{
    using Internal                             = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath       sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath       projectRoot   = sw::test::makeScratchPath( "Project" );
    const sw::vector<sw::string> listSession{ sessionFolder.str() };
    SW_REQUIRE_TRUE( Internal::makeEntry( projectRoot.str() ).write( sessionFolder.str() ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeEditedSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_FALSE( listEntry[0]._refusal.empty() );

    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, sw::test::makeScratchPath( "OtherProject" ).str(), &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_FALSE( listEntry[0]._refusal.empty() );
}

/** @brief [EditorDocumentAutosaveTest] 상태 문서의 사본은 패널 id 를 싣고, 파일 id 는 프로젝트 기준 경로로 적혀 같은 파일로 풀린다(절대 경로를 사본 이름에 쓰지 않는다) */
SW_TEST_CASE( EditorDocumentAutosaveTest, StateDocumentCopyKeepsItsPanel )
{
    using Internal                             = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath       sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath       projectRoot   = sw::test::makeScratchPath( "Project" );
    const sw::vector<sw::string> listSession{ sessionFolder.str() };
    const sw::AbsolutePath       configPath = sw::ProcessRoots::get()._project.join( "Config/Engine/Probe.json" );
    auto                         entry      = Internal::makeEntry( projectRoot.str() );
    entry._documentPath                     = sw::editor::EditorDocumentAutosaveEntry::makeFileID( configPath.str() );
    entry._panelID                          = "projectsettings";
    SW_EXPECT_EQUAL( sw::string( "Config/Engine/Probe.json" ), entry._documentPath );
    const sw::optional<sw::AbsolutePath> resolved = sw::editor::EditorDocumentAutosaveEntry::resolveFileID( entry._documentPath );
    SW_REQUIRE_TRUE( resolved.has_value() );
    SW_EXPECT_TRUE( sw::FileUtil::isSameDiskPath( resolved->str(), configPath.str() ) );
    SW_REQUIRE_TRUE( entry.write( sessionFolder.str() ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_EQUAL( sw::string( "projectsettings" ), listEntry[0]._panelID );
    SW_EXPECT_EQUAL( entry._documentPath, listEntry[0]._documentPath );
}
