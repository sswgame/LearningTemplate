#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineProviderFile.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    bool OnlineProviderFile::readEntries( const std::optional<AbsolutePath>& providersFile, const std::optional<AbsolutePath>& trustFile, const utf8* pProvidersOption,
                                          const utf8* pTrustOption, JSONDocument& outDocument, vector<JSONValue>& outListEntry, string& outError )
    {
        outListEntry.clear();
        if ( providersFile.has_value() == false )
        {
            if ( trustFile.has_value() == false )
                return true;
            outError = formatToString( "%# is set but %# is not", pTrustOption, pProvidersOption );
            return false;
        }
        string jsonText;
        if ( FileUtil::readTextFile( providersFile->str(), jsonText ) == false )
        {
            outError = formatToString( "cannot read the %# file %#", pProvidersOption, providersFile->c_str() );
            return false;
        }
        if ( outDocument.tryParse( jsonText ) == false )
        {
            outError = formatToString( "%# (%#) is not JSON", pProvidersOption, providersFile->c_str() );
            return false;
        }
        const JSONValue providers = outDocument.getRoot().get( "providers" );
        if ( providers.isArray() == false )
        {
            outError = formatToString( "%# (%#) needs a 'providers' array", pProvidersOption, providersFile->c_str() );
            return false;
        }
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JSONValue entry = providers.at( index );
            if ( entry.isObject() == false )
            {
                outError = formatToString( "%# (%#) has a provider entry that is not an object", pProvidersOption, providersFile->c_str() );
                return false;
            }
            outListEntry.push_back( entry );
        }
        return true;
    }
} // namespace sw
