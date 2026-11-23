// 상태 문서 패널의 자동 저장 사본과 복원입니다(ImGui 없음).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"
#include "Editor/Common/GUI/Panel/EditorStateDocumentPanel.h"

#include "Engine/Content/ContentHash.h"

namespace sw::editor
{
    DocumentAutosaveResult EditorStateDocumentPanel::takeAutosaveCopies( vector<EditorDocumentAutosaveEntry>& inoutListEntry )
    {
        vector<string> listPath;
        if ( isDocumentDirty() )
            collectFilesToSave( listPath );
        removeStaleCopies( listPath );
        DocumentAutosaveResult result = DocumentAutosaveResult::Nothing;
        for ( const string& path : listPath )
        {
            expected<string> text = tryMakeFileText( path );
            if ( text.has_value() == false )
            {
                SW_LOG_WARNING_ID( "EditorStateDocumentPanel.CopyTextFailed", "Autosave: %# has no copy of %# - %# (%#)", getPanelTitle(), path, text.error().message(), text.error().detail() );
                continue;
            }
            const uint64 textHash   = ContentHash::computeBytes( reinterpret_cast<const uint8*>( text->data() ), text->size() );
            uint64&      copiedHash = _mapCopyTextHash[path];
            if ( copiedHash == textHash )
                continue;
            copiedHash = textHash;
            EditorDocumentAutosaveEntry entry{};
            entry._documentPath = EditorDocumentAutosaveEntry::makeFileID( path );
            entry._text         = std::move( *text );
            inoutListEntry.push_back( std::move( entry ) );
            result = DocumentAutosaveResult::Ready;
        }
        return result;
    }

    void EditorStateDocumentPanel::flushDocumentInteraction()
    {
        string_view documentPath;
        string_view text;
        if ( EditorAutosaveCommands::peekPendingDocumentRestore( documentPath, text ) )
        {
            const optional<AbsolutePath> filePath = EditorDocumentAutosaveEntry::resolveFileID( documentPath );
            if ( filePath.has_value() && isEditingFile( filePath->str() ) )
            {
                const string fileID{ documentPath };
                string       takenText;
                if ( applyFileText( filePath->str(), string{ text } ) )
                {
                    (void)EditorAutosaveCommands::takePendingDocumentRestore( fileID, takenText ); // 방금 본 청이다 — 사본과 후보를 지운다
                    markDocumentDirty();
                }
                else
                {
                    EditorAutosaveCommands::cancelPendingDocumentRestore(); // 청만 거둔다 — 사본은 복구 창에 남는다(프레임마다 다시 읽지 않게)
                    SW_LOG_WARNING_ID( "EditorStateDocumentPanel.CopyNotApplied", "Autosave: the copy of %# could not be applied to %#", fileID, getPanelTitle() );
                }
            }
        }
        if ( isDocumentDirty() == false )
            removeStaleCopies( {} );
    }

    void EditorStateDocumentPanel::removeStaleCopies( const vector<string>& listPath )
    {
        vector<string> listStale;
        for ( const auto& [path, textHash] : _mapCopyTextHash )
        {
            if ( VectorUtil::contains( listPath, path ) == false )
                listStale.push_back( path );
        }
        for ( const string& path : listStale )
        {
            EditorDocumentAutosaveEntry::remove( EditorAutosaveCommands::getSessionFolder(), EditorDocumentAutosaveEntry::makeFileID( path ) );
            _mapCopyTextHash.erase( path );
        }
    }
} // namespace sw::editor
