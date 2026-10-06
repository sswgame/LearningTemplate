#include "pch.h"

#include "Engine/Utility/Json/ConfigKeyDoc.h"

#include "Core/Log/Logger.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        SW_LOG_CALLER( "ConfigKeyDoc" );
    } // namespace
} // namespace sw

namespace sw
{
    bool ConfigKeyDocUtil::hasOnlyKnownKeys( const JsonValue& object, const ConfigKeyDoc* pArrKeyDoc, size_t keyCount, string_view context, string* pOutUnknownKey )
    {
        bool bAllKnown = true;
        for ( const string& memberName : object.getMemberNames() )
        {
            bool bKnown = false;
            for ( size_t keyIndex = 0; keyIndex < keyCount && bKnown == false; ++keyIndex )
                bKnown = memberName == pArrKeyDoc[keyIndex]._pKey;
            if ( bKnown )
                continue;
            string known;
            for ( size_t keyIndex = 0; keyIndex < keyCount; ++keyIndex )
            {
                if ( keyIndex != 0 )
                    known += ", ";
                known += pArrKeyDoc[keyIndex]._pKey;
            }
            SW_LOG_ERROR( "%#: unknown key '%#' (known: %#)", context, memberName.c_str(), known.c_str() );
            if ( pOutUnknownKey != nullptr && bAllKnown )
                *pOutUnknownKey = memberName;
            bAllKnown = false;
        }
        return bAllKnown;
    }
} // namespace sw
