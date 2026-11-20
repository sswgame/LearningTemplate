#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Runtime/Foundation/Framework/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogRoot.h"

namespace sw
{
    namespace
    {
        /** @brief 루트 원소 이름입니다. */
        constexpr const utf8* kMinigameRootName = "Minigame";
    } // namespace

    bool MinigameDefinition::loadFromAsset( const AssetID& assetID, MinigameDefinition& outDefinition )
    {
        MinigameDefinition definition;
        if ( GameDataXML::loadFromAsset( definition, &MinigameDefinition::loadRoot, assetID, kMinigameRootName ) == false )
            return false;
        outDefinition = std::move( definition );
        return true;
    }

    bool MinigameDefinition::loadFromXMLText( string_view xmlText, string_view sourceName, MinigameDefinition& outDefinition )
    {
        MinigameDefinition definition;
        if ( GameDataXML::loadFromXMLText( definition, &MinigameDefinition::loadRoot, xmlText, sourceName, kMinigameRootName ) == false )
            return false;
        outDefinition = std::move( definition );
        return true;
    }

    bool MinigameDefinition::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 루트 속성은 스키마(빠진 필수 · 모르는 속성 · 깨진 값은 오류)다. 자식 원소는 없다 — 적힌 것은 모두 오타로 알린다.
        const bool bValid = root.readProperties( *this, sourceName );
        return XMLNameCheck::reportUnknownChildren( root.getRawNode(), nullptr, 0, sourceName ) && bValid;
    }

    bool MinigameDefinition::validate( const GameFeatureRegistry& features, string_view sourceName ) const
    {
        bool bValid = true;
        for ( const hashed_string& feature : _listFeature )
        {
            if ( features.findFeature( feature.view() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "MinigameDefinition.UnknownFeature", "%#: game feature '%#' is not registered (SW_GAME_FEATURE) - check the name or the kit module", sourceName, feature.c_str() );
            bValid = false;
        }
        // 같은 이름을 두 번 켜면 같은 서비스를 두 번 걸어 둘째가 진다 — 읽을 때 막는다.
        for ( const vector<hashed_string>* pList : { &_listFeature, &_listInputContext, &_listCatalog } )
        {
            const hashed_string* pDuplicate = VectorUtil::findDuplicate( *pList );
            if ( pDuplicate == nullptr )
                continue;
            SW_LOG_ERROR_ID( "MinigameDefinition.DuplicateName", "%#: '%#' is listed twice", sourceName, pDuplicate->c_str() );
            bValid = false;
        }
        if ( _map.isSet() == false || AssetIO::hasAsset( AssetID{ _map.get() } ) == false )
        {
            SW_LOG_ERROR_ID( "MinigameDefinition.MapMissing", "%#: map '%#' does not exist", sourceName, _map.isSet() ? _map.c_str() : "" );
            bValid = false;
        }
        for ( const string& document : _listUI )
        {
            if ( AssetIO::hasAsset( AssetID{ document } ) )
                continue;
            SW_LOG_ERROR_ID( "MinigameDefinition.UIMissing", "%#: UI document '%#' does not exist", sourceName, document.c_str() );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw
