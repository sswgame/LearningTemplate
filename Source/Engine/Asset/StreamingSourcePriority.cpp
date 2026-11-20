#include "pch.h"

#include "Engine/Asset/StreamingSourcePriority.h"

#include "Core/File/AsyncFileIO.h"

namespace sw
{
    AsyncIOPriority toAsyncIOPriority( const StreamingSourcePriority priority )
    {
        switch ( priority )
        {
            case StreamingSourcePriority::Highest:
            case StreamingSourcePriority::High:
                return AsyncIOPriority::High;
            case StreamingSourcePriority::Normal:
                return AsyncIOPriority::Normal;
            case StreamingSourcePriority::Low:
            case StreamingSourcePriority::Lowest:
                return AsyncIOPriority::Low;
        }
        return AsyncIOPriority::Normal;
    }
} // namespace sw
