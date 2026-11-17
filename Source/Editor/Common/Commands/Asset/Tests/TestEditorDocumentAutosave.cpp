// EditorDocumentAutosaveTest — 문서 패널 자동 저장 사본의 디스크 규칙(감사 E-1): 쓴 사본은 복구 후보로 읽히고 글이 같으며, 정상 저장(remove)하면 사라지고,
// 다른 프로젝트 · 사본 뒤에 바뀐 원본은 거절 이유가 붙는다. 스크래치 폴더만 쓴다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"

namespace
{
    struct EditorDocumentAutosaveTestInternal
    {
        static constexpr sw::uint64 kSourceHash = 0x1234u;

        static sw::uint64 computeKeptSourceHash( sw::string_view /*documentPath*/ ) { return kSourceHash; }
        static sw::uint64 computeEditedSourceHash( sw::string_view /*documentPath*/ ) { return kSourceHash + 1u; }

        static sw::editor::EditorDocumentAutosaveEntry makeEntry( sw::string_view projectRoot )
        {
            sw::editor::EditorDocumentAutosaveEntry entry{};
            entry._documentPath      = "game/empty/materials/probe.material";
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
    const sw::string             sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::string             projectRoot   = sw::test::makeScratchPath( "Project" );
    const auto                   entry         = Internal::makeEntry( projectRoot );
    const sw::vector<sw::string> listSession{ sessionFolder };
    SW_REQUIRE_TRUE( entry.write( sessionFolder ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot, &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._documentPath == entry._documentPath );
    SW_EXPECT_TRUE( listEntry[0]._refusal.empty() );
    sw::string text;
    SW_REQUIRE_TRUE( listEntry[0].readText( text ) );
    SW_EXPECT_TRUE( text == entry._text );

    sw::editor::EditorDocumentAutosaveEntry::remove( sessionFolder, entry._documentPath );
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot, &Internal::computeKeptSourceHash, listEntry );
    SW_EXPECT_EQUAL( size_t{ 0 }, listEntry.size() ); // 저장한 원본이 사본보다 새것이다 — 다음 기동이 묻지 않는다
}

/** @brief [EditorDocumentAutosaveTest] 다른 프로젝트의 사본 · 사본 뒤에 바뀐 원본에는 거절 이유가 붙는다(남의 일을 덮지 않는다) */
SW_TEST_CASE( EditorDocumentAutosaveTest, ForeignProjectAndChangedSourceAreRefused )
{
    using Internal                             = EditorDocumentAutosaveTestInternal;
    const sw::string             sessionFolder = sw::test::makeScratchPath( "DocumentAutosave" );
    const sw::string             projectRoot   = sw::test::makeScratchPath( "Project" );
    const sw::vector<sw::string> listSession{ sessionFolder };
    SW_REQUIRE_TRUE( Internal::makeEntry( projectRoot ).write( sessionFolder ) );

    sw::vector<sw::editor::EditorDocumentAutosaveEntry> listEntry;
    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, projectRoot, &Internal::computeEditedSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_FALSE( listEntry[0]._refusal.empty() );

    sw::editor::EditorDocumentAutosaveEntry::scan( listSession, sw::test::makeScratchPath( "OtherProject" ), &Internal::computeKeptSourceHash, listEntry );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listEntry.size() );
    SW_EXPECT_FALSE( listEntry[0]._refusal.empty() );
}
