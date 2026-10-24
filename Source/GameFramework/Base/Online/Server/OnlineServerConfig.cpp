#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        struct OnlineServerConfigInternal
        {
            static constexpr string_view kJSONExtension   = ".json";
            static constexpr string_view kOnlineExtension = ".online.json";
        };
    } // namespace
} // namespace sw

namespace sw
{
    OnlineServerKitOptionField::OnlineServerKitOptionField( const utf8* pKey, int32& target )
        : _pKey{ pKey }
        , _pTarget{ &target }
        , _kind{ Kind::Int }
    {
    }

    OnlineServerKitOptionField::OnlineServerKitOptionField( const utf8* pKey, bool& target )
        : _pKey{ pKey }
        , _pTarget{ &target }
        , _kind{ Kind::Bool }
    {
    }

    OnlineServerKitOptionField::OnlineServerKitOptionField( const utf8* pKey, string& target )
        : _pKey{ pKey }
        , _pTarget{ &target }
        , _kind{ Kind::Text }
    {
    }

    OnlineServerKitOptionField::OnlineServerKitOptionField( const utf8* pKey, vector<string>& listTarget )
        : _pKey{ pKey }
        , _pTarget{ &listTarget }
        , _kind{ Kind::TextList }
    {
    }

    bool OnlineServerKitOptionField::readValues( const vector<const string*>& listValue, const string& kitName, string& outError ) const
    {
        if ( listValue.empty() )
            return true;
        const string& first = *listValue.front();
        switch ( _kind )
        {
            case Kind::Int:
            {
                if ( StringUtil::parseInt( first, *static_cast<int32*>( _pTarget ) ) )
                    return true;
                outError = "option '" + string( _pKey ) + "' of kit '" + kitName + "' must be an integer (got '" + first + "')";
                return false;
            }
            case Kind::Bool:
            {
                if ( StringUtil::tryParseBool( first, *static_cast<bool*>( _pTarget ) ) )
                    return true;
                outError = "option '" + string( _pKey ) + "' of kit '" + kitName + "' must be a boolean (0, 1, true or false; got '" + first + "')";
                return false;
            }
            case Kind::Text:
            {
                *static_cast<string*>( _pTarget ) = first;
                return true;
            }
            case Kind::TextList:
            {
                vector<string>& target = *static_cast<vector<string>*>( _pTarget );
                for ( const string* pValue : listValue )
                {
                    target.push_back( *pValue );
                }
                return true;
            }
        }
        return true;
    }

    bool OnlineServerKitEntry::readOptions( std::initializer_list<OnlineServerKitOptionField> listField, string& outError ) const
    {
        for ( const OnlineServerOption& option : _listOption )
        {
            bool bKnown = false;
            for ( const OnlineServerKitOptionField& field : listField )
            {
                bKnown = bKnown || option._key == field.getKey();
            }
            if ( bKnown == false )
            {
                outError = "unknown option '" + option._key + "' of kit '" + _name + "'";
                return false;
            }
        }
        vector<const string*> listValue;
        for ( const OnlineServerKitOptionField& field : listField )
        {
            listValue.clear();
            for ( const OnlineServerOption& option : _listOption )
            {
                if ( option._key == field.getKey() )
                    listValue.push_back( &option._value );
            }
            if ( field.readValues( listValue, _name, outError ) == false )
                return false;
        }
        return true;
    }

    string OnlineServerConfig::makeConfigPath( string_view serverConfigPath )
    {
        string_view stem = serverConfigPath;
        if ( StringUtil::endsWith( stem, OnlineServerConfigInternal::kJSONExtension ) )
            stem = stem.substr( 0, stem.size() - OnlineServerConfigInternal::kJSONExtension.size() );
        string path{ stem };
        path += OnlineServerConfigInternal::kOnlineExtension;
        return path;
    }
} // namespace sw
