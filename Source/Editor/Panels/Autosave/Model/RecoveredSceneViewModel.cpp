#include "pch.h"

#include "Editor/Panels/Autosave/Model/RecoveredSceneViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"

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
            row._sceneID   = entry._sourceID;
            row._savedAge  = formatSavedAge( entry._savedTicks, nowTicks );
            row._copyCount = entry._generationCount;
            row._refusal   = entry._refusal;
            outState._listRow.push_back( std::move( row ) );
        }
    }

    void RecoveredSceneViewModel::restore( const uint32 rowIndex )
    {
        (void)EditorAutosaveCommands::restore( rowIndex ); // 거절 이유는 restore 가 알린다
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
