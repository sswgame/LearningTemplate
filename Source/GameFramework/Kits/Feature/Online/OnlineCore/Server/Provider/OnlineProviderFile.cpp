#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"

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
            StringBuilder<constant::buffer::kMax256> errorBuilder;
            errorBuilder.appendFormat( "%# is set but %# is not", pTrustOption, pProvidersOption );
            outError = string( errorBuilder.view() );
            return false;
        }
        string jsonText;
        if ( FileUtil::readTextFile( providersFile->str(), jsonText ) == false )
        {
            StringBuilder<constant::buffer::kMax256> errorBuilder;
            errorBuilder.appendFormat( "cannot read the %# file %#", pProvidersOption, providersFile->c_str() );
            outError = string( errorBuilder.view() );
            return false;
        }
        if ( outDocument.tryParse( jsonText ) == false )
        {
            StringBuilder<constant::buffer::kMax256> errorBuilder;
            errorBuilder.appendFormat( "%# (%#) is not JSON", pProvidersOption, providersFile->c_str() );
            outError = string( errorBuilder.view() );
            return false;
        }
        const JSONValue providers = outDocument.getRoot().get( "providers" );
        if ( providers.isArray() == false )
        {
            StringBuilder<constant::buffer::kMax256> errorBuilder;
            errorBuilder.appendFormat( "%# (%#) needs a 'providers' array", pProvidersOption, providersFile->c_str() );
            outError = string( errorBuilder.view() );
            return false;
        }
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JSONValue entry = providers.at( index );
            if ( entry.isObject() == false )
            {
                StringBuilder<constant::buffer::kMax256> errorBuilder;
                errorBuilder.appendFormat( "%# (%#) has a provider entry that is not an object", pProvidersOption, providersFile->c_str() );
                outError = string( errorBuilder.view() );
                return false;
            }
            outListEntry.push_back( entry );
        }
        return true;
    }
} // namespace sw
