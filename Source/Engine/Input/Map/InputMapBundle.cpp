#include "pch.h"

#include "Engine/Input/Map/InputMapBundle.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    InputMapExtensionRegistry::InputMapExtensionRegistry()
        : _mutex{}
        , _registration{ "InputMapExtensionRegistry", RegistrationOrder::ByOrderThenName, true }
    {
    }

    bool InputMapExtensionRegistry::registerEntry( const InputMapExtension* pExtension )
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.addLogged( pExtension, "Input map extension", pExtension != nullptr ? string_view{ pExtension->_pID } : string_view{} ) == RegistrationResult::Added;
    }

    void InputMapExtensionRegistry::unregisterEntry( const InputMapExtension* pExtension )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registration.remove( pExtension ); // 없으면 뺄 것이 없다
    }

    void InputMapExtensionRegistry::appendAssets( vector<AssetID>& inoutListAsset ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const InputMapExtension* pExtension : _registration.getItems() )
        {
            inoutListAsset.push_back( AssetID{ string_view{ pExtension->_pAssetPath } } );
        }
    }

    SW_ENGINE_SERVICE_SUBSYSTEM( InputMapExtensionRegistry, GameVisible, Services, (), SW_ENGINE_DEFAULT_FACTORY( InputMapExtensionRegistry ) );
} // namespace sw

namespace sw
{
    bool InputMapBundle::isSettingsFile( string_view settingsAssetID )
    {
        const string_view suffix = constant::path::kGameSettingsFile;
        return settingsAssetID.size() >= suffix.size() && settingsAssetID.substr( settingsAssetID.size() - suffix.size() ) == suffix;
    }

    bool InputMapBundle::collectAssets( const AssetID& settingsAssetID, vector<AssetID>& outListAsset )
    {
        outListAsset.clear();
        XMLDocument doc;
        string      absPath;
        if ( doc.loadResource( settingsAssetID, &absPath ) == false )
        {
            SW_LOG_ERROR_ID( "InputMapBundle.SettingsUnreadable", "Input map bundle: could not read %#", settingsAssetID.c_str() );
            return false;
        }
        const XMLNode   root = doc.getRoot( "GameSettings" );
        vector<AssetID> listListed;
        for ( XMLNode line = root.findChild( kSettingsElement ); line.isValid(); line = line.findNextSibling( kSettingsElement ) )
        {
            InputMapBundleLine params;
            if ( XMLFields::readProperties( line, params, absPath ) == false )
            {
                SW_LOG_ERROR_ID( "InputMapBundle.LineInvalid", "Input map bundle %#: an <%#> line needs path=\"<input map>\" and nothing else", absPath, kSettingsElement );
                return false;
            }
            listListed.push_back( AssetID{ string_view{ params._path } } );
        }

        vector<AssetID> listKit;
        engine::getSubsystem<InputMapExtensionRegistry>().appendAssets( listKit );
        if ( listListed.empty() )
        {
            if ( listKit.empty() )
                return true;
            SW_LOG_ERROR_ID( "InputMapBundle.KitWithoutBase", "Input map bundle %#: a loaded kit declares %# but the game lists no <%#> base map", absPath, listKit.front().c_str(), kSettingsElement );
            return false;
        }
        // 기본 맵 → 키트 → 나머지 줄. 게임 차이가 맨 뒤라 키트의 액션도 덮을 수 있다.
        outListAsset.push_back( listListed.front() );
        outListAsset.insert( outListAsset.end(), listKit.begin(), listKit.end() );
        outListAsset.insert( outListAsset.end(), listListed.begin() + 1, listListed.end() );
        return true;
    }

    bool InputMapBundle::loadFromSettings( InputMap& map, const AssetID& settingsAssetID )
    {
        vector<AssetID> listAsset;
        if ( collectAssets( settingsAssetID, listAsset ) == false )
            return false;
        return listAsset.empty() || map.loadBundle( listAsset );
    }

    bool InputMapBundle::checkSettings( const AssetID& settingsAssetID )
    {
        InputMap scratch; // 입력 장치에 붙이지 않는다 — 읽기와 겹침 검사만 한다
        return loadFromSettings( scratch, settingsAssetID );
    }

    bool InputMapBundle::checkPack( string_view packRoot )
    {
        return checkSettings( AssetID{ FileUtil::joinPath( packRoot, constant::path::kGameSettingsFile ) } );
    }
} // namespace sw
