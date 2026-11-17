// 문서 패널 자동 저장 사본의 디스크 규칙과 쓰기 잡입니다.
#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDocumentAutosaveInternal
        {
            static constexpr const utf8* kTextSuffix = ".autosave.txt";
            static constexpr const utf8* kMetaSuffix = ".meta";

            static string makeFolder( string_view sessionFolder ) { return FileUtil::joinPath( sessionFolder, EditorDocumentAutosaveEntry::kFolderName ); }
            static string makePath( string_view sessionFolder, string_view documentPath, string_view suffix )
            {
                return FileUtil::joinPath( makeFolder( sessionFolder ), EditorAutosaveCommands::makeSlotName( documentPath ) + string{ suffix } );
            }

            /** @brief 메타 한 줄(`key=value`)을 읽습니다. 없으면 빈 글입니다. */
            static string readMetaValue( string_view metaText, string_view key )
            {
                size_t lineStart = 0;
                while ( lineStart < metaText.size() )
                {
                    size_t lineEnd = metaText.find( '\n', lineStart );
                    if ( lineEnd == string_view::npos )
                        lineEnd = metaText.size();
                    const string_view line = metaText.substr( lineStart, lineEnd - lineStart );
                    if ( line.size() > key.size() && line.substr( 0, key.size() ) == key && line[key.size()] == '=' )
                        return string( line.substr( key.size() + 1 ) );
                    lineStart = lineEnd + 1;
                }
                return string();
            }
        };
    } // namespace

    bool EditorDocumentAutosaveEntry::write( string_view sessionFolder ) const
    {
        using Internal = EditorDocumentAutosaveInternal;
        if ( _documentPath.empty() || FileUtil::ensureDirectoryExists( Internal::makeFolder( sessionFolder ) ) == false )
            return false;
        const string textPath = Internal::makePath( sessionFolder, _documentPath, Internal::kTextSuffix );
        if ( FileUtil::writeTextFile( textPath, _text ) == false )
            return false;
        StringBuilder<constant::buffer::kMax512> metaBuilder;
        metaBuilder.appendFormat( "document=%#\nproject=%#\nsourceHash=%#\nsaved=%#\n", _documentPath, _projectRoot, _sourceContentHash,
                                  FileUtil::getCurrentFileWriteTime() );
        if ( FileUtil::writeTextFile( Internal::makePath( sessionFolder, _documentPath, Internal::kMetaSuffix ), metaBuilder.view() ) == false )
        {
            (void)FileUtil::tryRemoveFile( textPath ); // 메타 없는 사본은 어디 것인지 모른다
            return false;
        }
        return true;
    }

    void EditorDocumentAutosaveEntry::remove( string_view sessionFolder, string_view documentPath )
    {
        using Internal = EditorDocumentAutosaveInternal;
        if ( sessionFolder.empty() || documentPath.empty() )
            return;
        for ( const utf8* pSuffix : { Internal::kTextSuffix, Internal::kMetaSuffix } )
        {
            const string path = Internal::makePath( sessionFolder, documentPath, pSuffix );
            if ( FileUtil::exists( path ) )
                (void)FileUtil::tryRemoveFile( path );
        }
    }

    void EditorDocumentAutosaveEntry::scan( const vector<string>& listSessionFolder, string_view projectRoot, uint64 ( *computeSourceHash )( string_view documentPath ),
                                            vector<EditorDocumentAutosaveEntry>& outListEntry )
    {
        using Internal = EditorDocumentAutosaveInternal;
        outListEntry.clear();
        for ( const string& sessionFolder : listSessionFolder )
        {
            vector<string> listFile;
            if ( FileUtil::collectFiles( Internal::makeFolder( sessionFolder ), "", listFile, false ) == false )
                continue; // 문서 사본이 없는 세션이다
            for ( const string& file : listFile )
            {
                if ( StringUtil::endsWith( file, Internal::kMetaSuffix ) == false )
                    continue;
                string metaText;
                if ( FileUtil::readTextFile( file, metaText ) == false )
                    continue;
                EditorDocumentAutosaveEntry entry{};
                entry._documentPath = Internal::readMetaValue( metaText, "document" );
                entry._projectRoot  = Internal::readMetaValue( metaText, "project" );
                (void)StringUtil::parseUint64( Internal::readMetaValue( metaText, "sourceHash" ), entry._sourceContentHash ); // 없으면 0 — 새 문서로 본다
                (void)StringUtil::parseInt64( Internal::readMetaValue( metaText, "saved" ), entry._savedTicks );              // 없으면 0 — 창이 "-" 로 보인다
                entry._textAbs = file.substr( 0, file.size() - string_view( Internal::kMetaSuffix ).size() ) + Internal::kTextSuffix;
                if ( entry._documentPath.empty() || FileUtil::exists( entry._textAbs ) == false )
                    continue; // 짝이 없는 메타 — 복구할 글이 없다
                // 다른 프로젝트 · 바뀐 원본에는 적용하지 않는다(씬 자동 저장과 같은 규칙) — 사본이 남의 일을 덮지 않게.
                if ( FileUtil::isSameDiskPath( entry._projectRoot, projectRoot ) == false )
                    entry._refusal = "it was written for another project";
                else if ( computeSourceHash( entry._documentPath ) != entry._sourceContentHash )
                    entry._refusal = "the original document changed after this copy";
                outListEntry.push_back( std::move( entry ) );
            }
        }
    }

    bool EditorDocumentAutosaveEntry::readText( string& outText ) const
    {
        return FileUtil::readTextFile( _textAbs, outText );
    }

    void EditorDocumentAutosaveWriteJob::request( EditorDocumentAutosaveBatch batch )
    {
        const uint32 generation = beginRequest( std::move( batch ) );
        submitJob( "EditorDocumentAutosaveWrite", SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorDocumentAutosaveWriteJob::runJob ), MakeTaskArgs( _pState, generation ) );
    }

    void EditorDocumentAutosaveWriteJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State>     pState     = args.get<shared_ptr<State>>( 0 );
        const uint32                generation = args.get<uint32>( 1 );
        EditorDocumentAutosaveBatch batch;
        if ( readInput( pState, generation, batch ) == false )
            return;
        uint32 writtenCount{ 0 };
        for ( const EditorDocumentAutosaveEntry& entry : batch._listEntry )
        {
            if ( entry.write( batch._sessionFolder ) )
                ++writtenCount;
            else
                SW_LOG_WARNING_ID( "EditorDocumentAutosave.CopyFailed", "Autosave: could not write a copy of document '%#'", entry._documentPath );
        }
        publish( pState, generation, std::move( writtenCount ) );
    }
} // namespace sw::editor
