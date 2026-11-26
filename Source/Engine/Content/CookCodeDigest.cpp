#include "pch.h"

#include "Engine/Content/CookCodeDigest.h"

#include "CookCodeDigest.gen.h"

namespace sw
{
    uint64 CookCodeDigest::find( string_view name )
    {
        for ( const CookCodeDigestRow& row : cookcode::kArrRow )
        {
            if ( name == row._pName )
                return row._digest;
        }
        return 0;
    }

    const utf8* CookCodeDigest::getShaderCompilerVersion()
    {
        return cookcode::kShaderCompilerVersion;
    }
} // namespace sw
