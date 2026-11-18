#include "pch.h"

#include "Editor/Panels/Autosave/Model/RecoveredSceneViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Asset/EditorDocumentAutosave.h"
#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

namespace sw::editor
{
    void RecoveredSceneViewModel::gather( RecoveredSceneViewState& outState )
    {
        outState._listRow.clear();
        const int64 nowTicks = FileUtil::getCurrentFileWriteTime();
        for ( const EditorAutosaveEntry& entry : EditorAutosaveCommands::getRecoverable() )
        {
            RecoveredSceneRow row{};
            row._sceneID   = entry._sourceID.empty() ? string( "Untitled (never saved)" ) : entry._sourceID;
            row._savedAge  = formatSavedAge( entry._savedTicks, nowTicks );
            row._copyCount = entry._generationCount;
            row._refusal   = entry._refusal;
            outState._listRow.push_back( std::move( row ) );
        }
        outState._listDocumentRow.clear();
        for ( const EditorDocumentAutosaveEntry& entry : EditorAutosaveCommands::getRecoverableDocuments() )
        {
            RecoveredDocumentRow row{};
            row._documentPath = entry._documentPath;
            row._savedAge     = formatSavedAge( entry._savedTicks, nowTicks );
            row._refusal      = entry._refusal;
            outState._listDocumentRow.push_back( std::move( row ) );
        }
    }

    void RecoveredSceneViewModel::restoreDocument( const uint32 rowIndex )
    {
        (void)EditorAutosaveCommands::restoreDocument( rowIndex ); // 거절 이유는 restoreDocument 가 알린다
    }

    void RecoveredSceneViewModel::discardDocument( const uint32 rowIndex )
    {
        EditorAutosaveCommands::discardDocument( rowIndex );
    }

    void RecoveredSceneViewModel::restore( const uint32 rowIndex )
    {
        (void)EditorAutosaveCommands::requestRestore( rowIndex ); // 여는 일은 게임 단계가 한다(거절 이유는 그때 알린다). 범위 밖이면 그린 줄이 아니다
    }

    void RecoveredSceneViewModel::discard( const uint32 rowIndex )
    {
        EditorAutosaveCommands::discard( rowIndex );
    }

    string RecoveredSceneViewModel::formatSavedAge( const int64 savedTicks, const int64 nowTicks )
    {
        if ( savedTicks <= 0 )
            return string( "-" );
        const int64 seconds = ( nowTicks - savedTicks ) / FileUtil::kFileTimeTicksPerSecond;
        if ( seconds < constant::time::kSecondsPerMinute )
        {
            StringBuilder<constant::buffer::kMax256> resultBuilder3;
            resultBuilder3.appendFormat( "%# s ago", seconds < 0 ? 0 : seconds );
            return string( resultBuilder3.view() );
        }
        if ( seconds < constant::time::kSecondsPerHour )
        {
            StringBuilder<constant::buffer::kMax256> resultBuilder2;
            resultBuilder2.appendFormat( "%# min ago", seconds / constant::time::kSecondsPerMinute );
            return string( resultBuilder2.view() );
        }
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "%# h ago", seconds / constant::time::kSecondsPerHour );
        return string( resultBuilder.view() );
    }
} // namespace sw::editor
