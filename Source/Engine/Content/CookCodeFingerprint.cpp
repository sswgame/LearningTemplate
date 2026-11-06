#include "pch.h"

#include "Engine/Content/CookCodeFingerprint.h"

#include "CookCodeFingerprint.gen.h"

namespace sw
{
    uint64 CookCodeFingerprint::find( string_view name )
    {
        for ( const CookCodeFingerprintRow& row : cookcode::kArrRow )
        {
            if ( name == row._pName )
                return row._fingerprint;
        }
        return 0;
    }

    const utf8* CookCodeFingerprint::getShaderCompilerVersion()
    {
        return cookcode::kShaderCompilerVersion;
    }
} // namespace sw
