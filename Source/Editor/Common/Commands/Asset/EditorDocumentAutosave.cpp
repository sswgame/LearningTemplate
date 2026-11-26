// 문서 패널 자동 저장 사본의 디스크 규칙과 쓰기 잡입니다.
#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

#include "Engine/Content/AssetRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct EditorDocumentAutosaveInternal
        {
            static constexpr const utf8* kTextSuffix = ".autosave.txt";
            static constexpr const utf8* kMetaSuffix = ".meta";

            static AbsolutePath makeFolder( const AbsolutePath& sessionFolder ) { return sessionFolder.join( EditorDocumentAutosaveEntry::kFolderName ); }
            static AbsolutePath makePath( const AbsolutePath& sessionFolder, string_view documentPath, string_view suffix )
            {
                StringBuilder<constant::buffer::kMax256> fileNameBuilder;
                fileNameBuilder.appendFormat( "%#%#", EditorAutosaveCommands::makeSlotName( documentPath ), suffix );
                return makeFolder( sessionFolder ).join( fileNameBuilder.view() );
            }
        };
    } // namespace

    bool EditorDocumentAutosaveEntry::write( const AbsolutePath& sessionFolder ) const
    {
        using Internal = EditorDocumentAutosaveInternal;
        if ( _documentPath.empty() )
            return false;
        const AbsolutePath textPath = Internal::makePath( sessionFolder, _documentPath, Internal::kTextSuffix );
        if ( FileUtil::writeTextFile( textPath, _text ) == false )
            return false;
        StringBuilder<constant::buffer::kMax512> metaBuilder;
        metaBuilder.appendFormat( "document=%#\npanel=%#\nproject=%#\nsourceHash=%#\nsaved=%#\n", AssetRegistry::makeIndexedReferenceText( _documentPath ), _panelID, _projectRoot, _sourceContentHash,
                                  FileUtil::getCurrentFileWriteTime() );
        if ( FileUtil::writeTextFile( Internal::makePath( sessionFolder, _documentPath, Internal::kMetaSuffix ), metaBuilder.view() ) == false )
        {
            (void)FileUtil::tryRemoveFile( textPath ); // 메타 없는 사본은 어디 것인지 모른다
            return false;
        }
        return true;
    }

    void EditorDocumentAutosaveEntry::remove( const AbsolutePath& sessionFolder, string_view documentPath )
    {
        using Internal = EditorDocumentAutosaveInternal;
        if ( sessionFolder.empty() || documentPath.empty() )
            return;
        for ( const utf8* pSuffix : { Internal::kTextSuffix, Internal::kMetaSuffix } )
        {
            const AbsolutePath path = Internal::makePath( sessionFolder, documentPath, pSuffix );
            if ( FileUtil::exists( path ) )
                (void)FileUtil::tryRemoveFile( path );
        }
    }

    void EditorDocumentAutosaveEntry::scan( const vector<AbsolutePath>& listSessionFolder, string_view projectRoot, uint64 ( *computeSourceHash )( const EditorDocumentAutosaveEntry& entry ),
                                            vector<EditorDocumentAutosaveEntry>& outListEntry )
    {
        using Internal = EditorDocumentAutosaveInternal;
        outListEntry.clear();
        for ( const AbsolutePath& sessionFolder : listSessionFolder )
        {
            vector<AbsolutePath> listFile;
            if ( FileUtil::collectFiles( Internal::makeFolder( sessionFolder ), "", listFile, false ) == false )
                continue; // 문서 사본이 없는 세션이다
            for ( const AbsolutePath& file : listFile )
            {
                if ( StringUtil::endsWith( file.str(), Internal::kMetaSuffix ) == false )
                    continue;
                string metaText;
                if ( FileUtil::readTextFile( file, metaText ) == false )
                    continue;
                EditorDocumentAutosaveEntry entry{};
                entry._documentReference                      = EditorAutosaveCommands::findMetaValue( metaText, "document" );
                const expected<AssetReferenceTarget> document = AssetRegistry::readReferenceText( entry._documentReference );
                entry._documentPath                           = document.has_value() ? document->_path : entry._documentReference;
                entry._panelID                                = EditorAutosaveCommands::findMetaValue( metaText, "panel" );
                entry._projectRoot                            = EditorAutosaveCommands::findMetaValue( metaText, "project" );
                (void)StringUtil::parseUint64( EditorAutosaveCommands::findMetaValue( metaText, "sourceHash" ), entry._sourceContentHash ); // 없으면 0 — 새 문서로 본다
                (void)StringUtil::parseInt64( EditorAutosaveCommands::findMetaValue( metaText, "saved" ), entry._savedTicks );              // 없으면 0 — 창이 "-" 로 보인다
                entry._textAbs = file.removeExtension().appendSuffix( Internal::kTextSuffix );
                if ( entry._documentPath.empty() || FileUtil::exists( entry._textAbs ) == false )
                    continue; // 짝이 없는 메타 — 복구할 글이 없다
                // 다른 프로젝트 · 바뀐 원본에는 적용하지 않는다(씬 자동 저장과 같은 규칙) — 사본이 남의 일을 덮지 않게.
                if ( FileUtil::isSameDiskPath( entry._projectRoot, projectRoot ) == false )
                    entry._refusal = "it was written for another project";
                else if ( document.has_value() == false )
                    entry._refusal = "the original document is gone";
                else if ( computeSourceHash( entry ) != entry._sourceContentHash )
                    entry._refusal = "the original document changed after this copy";
                outListEntry.push_back( std::move( entry ) );
            }
        }
    }

    bool EditorDocumentAutosaveEntry::isCopyOf( string_view documentPath ) const
    {
        return _textAbs == EditorDocumentAutosaveInternal::makePath( _textAbs.getParent().getParent(), documentPath, EditorDocumentAutosaveInternal::kTextSuffix );
    }

    string EditorDocumentAutosaveEntry::makeFileID( string_view absolutePath )
    {
        string relative;
        return FileUtil::tryMakePathUnderRoot( ProcessRoots::get()._project.str(), absolutePath, relative ) ? relative : FileUtil::normalizeSeparators( absolutePath );
    }

    optional<AbsolutePath> EditorDocumentAutosaveEntry::resolveFileID( string_view fileID )
    {
        return AbsolutePath::tryResolve( fileID, ProcessRoots::get()._project );
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
