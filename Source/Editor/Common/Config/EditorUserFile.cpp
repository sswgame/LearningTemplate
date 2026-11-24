#include "pch.h"

#include "Editor/Common/Config/EditorUserFile.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw::editor
{
    bool EditorUserFile::quarantineCorruptFile( const AbsolutePath& filePath, string_view reason )
    {
        const AbsolutePath                       source( filePath );
        StringBuilder<constant::buffer::kMax512> destinationBuilder;
        destinationBuilder.appendFormat( "%#%#", source.str(), kCorruptSuffix );
        const AbsolutePath destination( FileUtil::toDiskPath( destinationBuilder.view() ) );
        if ( FileUtil::copyFile( source, destination ) == false || FileUtil::removeFile( source ) == false )
        {
            SW_LOG_ERROR_ID( "EditorUserFile.UserFileBrokenCould", "User file %# is broken (%#) and could not be moved to %# - it is left in place", source.str(), string( reason ), destination.str() );
            return false;
        }
        SW_LOG_ERROR_ID( "EditorUserFile.UserFileBrokenMoved", "User file %# is broken (%#) - moved it to %# and starting a new one", source.str(), string( reason ), destination.str() );
        return true;
    }
} // namespace sw::editor
