#include "pch.h"

#include "Editor/Common/Config/EditorUserFile.h"

#include "Core/File/FileUtil.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUserFile" );

    bool EditorUserFile::quarantineCorruptFile( string_view filePath, string_view reason )
    {
        const string source( filePath );
        const string destination = source + kCorruptSuffix;
        if ( FileUtil::copyFile( source, destination ) == false || FileUtil::removeFile( source ) == false )
        {
            SW_LOG_ERROR( "User file %# is broken (%#) and could not be moved to %# - it is left in place", source, string( reason ), destination );
            return false;
        }
        SW_LOG_ERROR( "User file %# is broken (%#) - moved it to %# and starting a new one", source, string( reason ), destination );
        return true;
    }
} // namespace sw::editor
