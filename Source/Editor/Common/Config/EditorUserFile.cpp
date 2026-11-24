#include "pch.h"

#include "Editor/Common/Config/EditorUserFile.h"

#include "Core/File/FileUtil.h"

namespace sw::editor
{
    bool EditorUserFile::quarantineCorruptFile( const AbsolutePath& filePath, string_view reason )
    {
        const AbsolutePath destination = filePath.appendSuffix( kCorruptSuffix );
        if ( FileUtil::copyFile( filePath, destination ) == false || FileUtil::removeFile( filePath ) == false )
        {
            SW_LOG_ERROR_ID( "EditorUserFile.UserFileBrokenCould", "User file %# is broken (%#) and could not be moved to %# - it is left in place", filePath.str(), string( reason ), destination.str() );
            return false;
        }
        SW_LOG_ERROR_ID( "EditorUserFile.UserFileBrokenMoved", "User file %# is broken (%#) - moved it to %# and starting a new one", filePath.str(), string( reason ), destination.str() );
        return true;
    }
} // namespace sw::editor
