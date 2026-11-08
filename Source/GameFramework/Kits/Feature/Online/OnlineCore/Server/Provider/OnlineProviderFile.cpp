#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"

#include "Core/File/FileUtil.h"

namespace sw
{
    namespace
    {
        struct OnlineProviderFileInternal
        {
            /** @brief 대상이 "<옵션> (<경로>)" 인 오류입니다. */
            static Error makeFileError( const utf8* pMessage, const utf8* pProvidersOption, const AbsolutePath& providersFile )
            {
                Error error{ ErrorKind::InvalidArgument, pMessage, pProvidersOption };
                error.appendDetail( " (" ).appendDetail( providersFile.str() ).appendDetail( ")" );
                return error;
            }
        };
    } // namespace

    expected<void> OnlineProviderFile::readEntries( const optional<AbsolutePath>& providersFile, const optional<AbsolutePath>& trustFile, const utf8* pProvidersOption,
                                                    const utf8* pTrustOption, JSONDocument& outDocument, vector<JSONValue>& outListEntry )
    {
        using Internal = OnlineProviderFileInternal;
        outListEntry.clear();
        if ( providersFile.has_value() == false )
        {
            if ( trustFile.has_value() == false )
                return {};
            Error error{ ErrorKind::InvalidArgument, "the trust file option is set but the providers file option is not", pTrustOption };
            error.appendDetail( " without " ).appendDetail( pProvidersOption );
            return unexpected( error );
        }
        string jsonText;
        if ( FileUtil::readTextFile( providersFile->str(), jsonText ) == false )
            return unexpected( Internal::makeFileError( "cannot read the providers file", pProvidersOption, *providersFile ) );
        if ( outDocument.tryParse( jsonText ) == false )
            return unexpected( Internal::makeFileError( "the providers file is not JSON", pProvidersOption, *providersFile ) );
        const JSONValue providers = outDocument.getRoot().get( "providers" );
        if ( providers.isArray() == false )
            return unexpected( Internal::makeFileError( "the providers file needs a 'providers' array", pProvidersOption, *providersFile ) );
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JSONValue entry = providers.at( index );
            if ( entry.isObject() == false )
                return unexpected( Internal::makeFileError( "the providers file has a provider entry that is not an object", pProvidersOption, *providersFile ) );
            outListEntry.push_back( entry );
        }
        return {};
    }
} // namespace sw
