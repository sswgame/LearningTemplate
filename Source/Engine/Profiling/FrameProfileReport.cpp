#include "pch.h"

#include "Engine/Profiling/FrameProfileReport.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw
{
    bool FrameProfileReport::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( JSONSerializer::saveToFile( absPath, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "FrameProfileReport.SaveFailed", "[Profile] could not write the profile report %#", absPath.str() );
            return false;
        }
        SW_LOG_INFO_ID( "FrameProfileReport.Saved", "[Profile] report saved to %# (%# frames, %# us/frame)", absPath.str(), _frames, _microPerFrame );
        return true;
    }
} // namespace sw
