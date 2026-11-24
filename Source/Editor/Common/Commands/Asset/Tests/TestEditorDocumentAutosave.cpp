// EditorDocumentAutosaveTest — 문서 패널 자동 저장 사본의 디스크 규칙(감사 E-1): 쓴 사본은 복구 후보로 읽히고 글이 같으며, 정상 저장(remove)하면 사라지고,
// 다른 프로젝트 · 사본 뒤에 바뀐 원본은 거절 이유가 붙는다. 스크래치 폴더만 쓴다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"

#include "Engine/Content/AssetRegistry.h"

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
    using Internal                                   = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath             sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath             projectRoot   = sw::test::makeScratchPath( "Project" );
    const auto                         entry         = Internal::makeEntry( projectRoot.str() );
    const sw::vector<sw::AbsolutePath> listSession{ sessionFolder };
    SW_REQUIRE_TRUE( entry.write( sessionFolder ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._documentPath == entry._documentPath );
    SW_EXPECT_TRUE( listEntry[0]._refusal.empty() );
    sw::string text;
    SW_REQUIRE_TRUE( listEntry[0].readText( text ) );
    SW_EXPECT_TRUE( text == entry._text );

    sw::editor::EditorDocumentAutosaveEntry::remove( sessionFolder, entry._documentPath );
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_EXPECT_EQUAL( size_t{ 0 }, listEntry.size() ); // 저장한 원본이 사본보다 새것이다 — 다음 기동이 묻지 않는다
}

/** @brief [EditorDocumentAutosaveTest] 다른 프로젝트의 사본 · 사본 뒤에 바뀐 원본에는 거절 이유가 붙는다(남의 일을 덮지 않는다) */
SW_TEST_CASE( EditorDocumentAutosaveTest, ForeignProjectAndChangedSourceAreRefused )
{
    using Internal                                   = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath             sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath             projectRoot   = sw::test::makeScratchPath( "Project" );
    const sw::vector<sw::AbsolutePath> listSession{ sessionFolder };
    SW_REQUIRE_TRUE( Internal::makeEntry( projectRoot.str() ).write( sessionFolder ) );

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
    using Internal                                   = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath             sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath             projectRoot   = sw::test::makeScratchPath( "Project" );
    const sw::vector<sw::AbsolutePath> listSession{ sessionFolder };
    const sw::AbsolutePath             configPath = sw::ProcessRoots::get()._project.join( "Config/Engine/Probe.json" );
    auto                               entry      = Internal::makeEntry( projectRoot.str() );
    entry._documentPath                           = sw::editor::EditorDocumentAutosaveEntry::makeFileID( configPath.str() );
    entry._panelID                                = "projectsettings";
    SW_EXPECT_EQUAL( sw::string( "Config/Engine/Probe.json" ), entry._documentPath );
    const sw::optional<sw::AbsolutePath> resolved = sw::editor::EditorDocumentAutosaveEntry::resolveFileID( entry._documentPath );
    SW_REQUIRE_TRUE( resolved.has_value() );
    SW_EXPECT_TRUE( sw::FileUtil::isSameDiskPath( resolved->str(), configPath.str() ) );
    SW_REQUIRE_TRUE( entry.write( sessionFolder ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_EQUAL( sw::string( "projectsettings" ), listEntry[0]._panelID );
    SW_EXPECT_EQUAL( entry._documentPath, listEntry[0]._documentPath );
}

/** @brief [EditorDocumentAutosaveTest] 문서를 옮긴 뒤에도 사본은 그 문서의 것이다 — 경로로 적으면 옛 자리에 새로 생긴 문서가 남의 사본을 이어받는다 */
SW_TEST_CASE( EditorDocumentAutosaveTest, CopyFollowsTheMovedDocument )
{
    using Internal                                   = EditorDocumentAutosaveTestInternal;
    const sw::AbsolutePath             sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::AbsolutePath             projectRoot   = sw::test::makeScratchPath( "Project" );
    const sw::vector<sw::AbsolutePath> listSession{ sessionFolder };
    const auto                         entry   = Internal::makeEntry( projectRoot.str() );
    const sw::string                   newPath = "game/playground/materials/moved/probe.material";
    sw::vector<sw::AssetRegistryLayer> listLayer;
    listLayer.push_back( sw::AssetRegistryLayer{ "test", { sw::AssetRegistryEntry{ sw::AssetGUID::create(), entry._documentPath } } } );
    sw::AssetRegistry previous = sw::AssetRegistry::install( std::move( *sw::AssetRegistry::merge( listLayer ) ) );
    SW_EXPECT_TRUE( entry.write( sessionFolder ) );
    SW_EXPECT_TRUE( sw::AssetRegistry::move( entry._documentPath, newPath ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_EXPECT_EQUAL( size_t{ 1 }, listEntry.size() );
    for ( const sw::editor::EditorDocumentAutosaveEntry& recovered : listEntry )
    {
        SW_EXPECT_TRUE( recovered._documentPath == newPath );
        SW_EXPECT_TRUE( recovered._refusal.empty() );
        SW_EXPECT_TRUE( recovered.isCopyOf( newPath ) );
        SW_EXPECT_FALSE( recovered.isCopyOf( entry._documentPath ) );
    }

    // 지운 문서(색인에 없다)의 사본은 다른 문서에 얹지 않는다.
    (void)sw::AssetRegistry::install( sw::AssetRegistry{} );
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot.str(), &Internal::computeKeptSourceHash, listEntry );
    SW_EXPECT_EQUAL( size_t{ 1 }, listEntry.size() );
    if ( listEntry.empty() == false )
        SW_EXPECT_FALSE( listEntry[0]._refusal.empty() );
    (void)sw::AssetRegistry::install( std::move( previous ) );
}
