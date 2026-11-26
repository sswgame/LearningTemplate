// EditorDocumentPanelSaveTest — 문서 저장은 기반 하나를 지난다: 파생은 문(`AssetWriter`)에 쓰기만 하고 읽기 전용 · 밖에서 바뀜 · 저장 실패 · 되돌리기 기준 · 자동 저장 사본은 기반이 맞춘다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetWriter.h"
#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/EditorStateDocumentPanel.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 파일 하나가 문서인 시험용 패널입니다. 그리지 않고 편집 · 저장 · 되돌리기만 부른다. */
    class SaveProbePanel final : public EditorDocumentPanel
    {
    public:
        explicit SaveProbePanel( const DiskPath& file )
            : EditorDocumentPanel{ EditorAssetType{}, false }
            , _file{ file }
            , _savedCount{ 0 }
            , _bFailsToWrite{ SW_FALSE }
        {
        }

        using EditorDocumentPanel::getDocumentCommandStack;

        /** @brief 파일을 읽어 엽니다(`reloadDocument`). */
        void open() { EditorDocumentPanel::reloadDocument(); }
        void edit( string_view text )
        {
            _text = string{ text };
            getDocumentEditRecorder().notifyEdited( "Probe Edit" );
        }
        void          setFailsToWrite( bool bFails ) { _bFailsToWrite = bFails ? SW_TRUE : SW_FALSE; }
        uint32        getSavedCount() const { return _savedCount; }
        const string& getText() const { return _text; }

    protected:
        void                drawDocumentContent() override {}
        ToolAssetLoadResult loadDocument() override { return FileUtil::readTextFile( _file, _text ) ? ToolAssetLoadResult::Loaded : ToolAssetLoadResult::Malformed; }
        [[nodiscard]] bool  saveDocument( AssetWriter& writer ) override
        {
            return writer.save( _file, [this]( const AbsolutePath& writable )
            { return _bFailsToWrite == SW_FALSE && FileUtil::writeTextFile( writable, _text ); } );
        }
        void collectFilesToSave( vector<DiskPath>& outListPath ) const override { outListPath.push_back( _file ); }
        void onDocumentSaved() override
        {
            ++_savedCount;
            EditorDocumentPanel::onDocumentSaved();
        }
        string captureDocumentText() const override { return _text; }
        void   applyDocumentText( string_view text ) override { _text = string{ text }; }

    private:
        DiskPath _file;
        string   _text;
        uint32   _savedCount;
        uint8    _bFailsToWrite : 1;
    };

    /** @brief 파일 하나를 고치는 시험용 상태 문서입니다. */
    class StateProbePanel final : public EditorStateDocumentPanel
    {
    public:
        explicit StateProbePanel( const DiskPath& file )
            : _file{ file }
        {
        }

        const utf8* getPanelTitle() const override { return "State Probe"; }
        void        edit( string_view text )
        {
            _text = string{ text };
            markDocumentDirty();
        }

    protected:
        void                           drawContent() override {}
        [[nodiscard]] bool             saveDocument( AssetWriter& writer ) override { return writer.writeText( _file, _text ); }
        void                           revertDocument() override {}
        void                           collectFilesToSave( vector<DiskPath>& outListPath ) const override { outListPath.push_back( _file ); }
        [[nodiscard]] expected<string> tryMakeFileText( const DiskPath& /*absolutePath*/ ) const override { return _text; }
        [[nodiscard]] bool             isEditingFile( const DiskPath& absolutePath ) override { return absolutePath == _file; }
        [[nodiscard]] bool             applyFileText( const DiskPath& /*absolutePath*/, const string& text ) override
        {
            _text = text;
            return true;
        }

    private:
        DiskPath _file;
        string   _text;
    };

    string readText( const DiskPath& path )
    {
        string text;
        SW_EXPECT_TRUE( FileUtil::readTextFile( path, text ) );
        return text;
    }
} // namespace

/** @brief [EditorDocumentPanelSaveTest] 저장하면 파일에 쓰고 dirty 를 지우며 그 글이 되돌리기의 새 기준이다 — 앞 글로 되돌리면 다시 미저장이다 */
SW_TEST_CASE( EditorDocumentPanelSaveTest, SavedTextIsTheNewUndoBaseline )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              file = domain.makeDiskPath( "doc.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "a" ) );
    SaveProbePanel panel{ file };
    panel.open();
    panel.edit( "ab" );
    SW_REQUIRE_TRUE( panel.isDocumentDirty() );

    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
    SW_EXPECT_EQUAL( 1u, panel.getSavedCount() );
    SW_EXPECT_EQUAL( readText( file ), string( "ab" ) );

    panel.getDocumentCommandStack().undo();
    SW_EXPECT_EQUAL( panel.getText(), string( "a" ) );
    SW_EXPECT_TRUE( panel.isDocumentDirty() ); // 디스크는 "ab" 다
    panel.getDocumentCommandStack().redo();
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() ); // 우리가 쓴 내용은 밖에서 바뀐 것이 아니다
}

/** @brief [EditorDocumentPanelSaveTest] 쓰기가 지거나 읽기 전용이면 파일 · dirty · 저장 뒤 알림(자동 저장 사본 지우기 · 핫 리로드)이 그대로다 */
SW_TEST_CASE( EditorDocumentPanelSaveTest, AFailedSaveKeepsTheEditAndTheFile )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              file = domain.makeDiskPath( "doc.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "a" ) );
    SaveProbePanel panel{ file };
    panel.open();
    panel.edit( "ab" );

    panel.setFailsToWrite( true );
    SW_EXPECT_FALSE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
    panel.setFailsToWrite( false );

    SW_REQUIRE_TRUE( FileUtil::setWritable( file, false ) );
    SW_EXPECT_LOG( "AssetWriter.ReadOnly" );
    SW_EXPECT_FALSE( panel.saveDocumentAndClearDirty() );
    SW_REQUIRE_TRUE( FileUtil::setWritable( file, true ) );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
    SW_EXPECT_EQUAL( 0u, panel.getSavedCount() );
    SW_EXPECT_EQUAL( readText( file ), string( "a" ) );

    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() ); // 거절된 저장이 기준을 흐리지 않았다
    SW_EXPECT_EQUAL( readText( file ), string( "ab" ) );
}

/** @brief [EditorDocumentPanelSaveTest] 연 뒤 밖에서 바뀐 파일은 첫 저장이 덮지 않고 알린다. 다시 읽으면 그 내용이 기준이고, 알고 다시 저장하면 덮는다 */
SW_TEST_CASE( EditorDocumentPanelSaveTest, AFileChangedOutsideIsNotOverwrittenSilently )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              file = domain.makeDiskPath( "doc.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "a" ) );
    SaveProbePanel panel{ file };
    panel.open();
    panel.edit( "ab" );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "outside" ) );
    SW_EXPECT_LOG( "AssetWriter.ChangedOnDisk" );
    SW_EXPECT_FALSE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_EQUAL( readText( file ), string( "outside" ) );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_EQUAL( readText( file ), string( "ab" ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( file, "outside again" ) );
    panel.open(); // 다시 읽었다
    panel.edit( "mine" );
    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_EQUAL( readText( file ), string( "mine" ) );
}

/** @brief [EditorDocumentPanelSaveTest] 상태 문서의 자동 저장 사본은 저장이 쓸 글 그대로이고, 저장해 깨끗해지면 낼 사본이 없다 */
SW_TEST_CASE( EditorDocumentPanelSaveTest, AStateDocumentCopiesTheTextItWouldSave )
{
    const test::ScopedScratchDomain     domain;
    const AbsolutePath                  file = domain.makeDiskPath( "state.txt" );
    StateProbePanel                     panel{ file };
    vector<EditorDocumentAutosaveEntry> listCopy;
    SW_EXPECT_TRUE( panel.takeAutosaveCopies( listCopy ) == DocumentAutosaveResult::Nothing );

    panel.edit( "state" );
    SW_EXPECT_TRUE( panel.takeAutosaveCopies( listCopy ) == DocumentAutosaveResult::Ready );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listCopy.size() );
    SW_EXPECT_EQUAL( listCopy[0]._text, string( "state" ) );

    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_EQUAL( readText( file ), listCopy[0]._text );
    listCopy.clear();
    SW_EXPECT_TRUE( panel.takeAutosaveCopies( listCopy ) == DocumentAutosaveResult::Nothing );
    SW_EXPECT_TRUE( listCopy.empty() );
}
