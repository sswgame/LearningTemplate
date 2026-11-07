#include "pch.h"

#include "Editor/Panels/Autosave/Model/RecoveredWorkViewModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

namespace sw::editor
{
    void RecoveredWorkViewModel::gather( RecoveredWorkViewState& outState )
    {
        outState._listRow.clear();
        const int64 nowTicks = FileUtil::getCurrentFileWriteTime();
        for ( const EditorAutosaveEntry& entry : EditorAutosaveCommands::getRecoverable() )
        {
            RecoveredWorkRow row{};
            row._sceneID   = entry._sourceID;
            row._savedAge  = formatSavedAge( entry._savedTicks, nowTicks );
            row._copyCount = entry._generationCount;
            row._refusal   = entry._refusal;
            outState._listRow.push_back( std::move( row ) );
        }
    }

    void RecoveredWorkViewModel::restore( const uint32 rowIndex )
    {
        (void)EditorAutosaveCommands::restore( rowIndex ); // 거절 이유는 restore 가 알린다
    }

    void RecoveredWorkViewModel::discard( const uint32 rowIndex )
    {
        EditorAutosaveCommands::discard( rowIndex );
    }

    string RecoveredWorkViewModel::formatSavedAge( const int64 savedTicks, const int64 nowTicks )
    {
        if ( savedTicks <= 0 )
            return string( "-" );
        const int64 seconds = ( nowTicks - savedTicks ) / FileUtil::kFileTimeTicksPerSecond;
        if ( seconds < constant::time::kSecondsPerMinute )
            return formatToString( "%# s ago", seconds < 0 ? 0 : seconds );
        if ( seconds < constant::time::kSecondsPerHour )
            return formatToString( "%# min ago", seconds / constant::time::kSecondsPerMinute );
        return formatToString( "%# h ago", seconds / constant::time::kSecondsPerHour );
    }
} // namespace sw::editor
