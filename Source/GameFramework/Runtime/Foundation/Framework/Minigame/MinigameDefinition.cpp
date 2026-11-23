#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameDefinition.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Input/Map/InputMap.h"
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

    bool MinigameDefinition::insertPortal( vector<MinigameDefinition>& inoutListPortal, MinigameDefinition definition )
    {
        size_t index = 0;
        while ( index < inoutListPortal.size() && inoutListPortal[index]._portalSlot < definition._portalSlot )
        {
            ++index;
        }
        if ( index < inoutListPortal.size() && inoutListPortal[index]._portalSlot == definition._portalSlot )
        {
            SW_LOG_ERROR_ID( "MinigameDefinition.PortalSlotTaken", "Minigame '%#': portalSlot %# is already taken by '%#' - pick a free slot, this entrance is not built", definition._name.c_str(),
                             definition._portalSlot, inoutListPortal[index]._name.c_str() );
            return false;
        }
        inoutListPortal.insert( inoutListPortal.begin() + static_cast<ptrdiff_t>( index ), std::move( definition ) );
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
        // 매치형 칸은 한 벌이다 — 배정만 있고 결과를 낼 곳이 없거나, 결과만 있고 배정이 없는 정의는 어느 쪽도 돌지 않는다. 매치는 서버의 판이라 세이브로 되살리지 않는다.
        const bool bMatchAny = isMatch() || _matchBoard.empty() == false || _matchScore.empty() == false;
        const bool bMatchAll = isMatch() && _matchBoard.empty() == false && _matchScore.empty() == false && _entry == MinigameEntry::SceneSwitch && _savePolicy == MinigameSavePolicy::Blocked;
        if ( bMatchAny && bMatchAll == false )
        {
            SW_LOG_ERROR_ID( "MinigameDefinition.MatchIncomplete", "%#: a match minigame needs matchMode, matchBoard and matchScore together, entry=\"SceneSwitch\" and save=\"Blocked\"", sourceName );
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

    bool MinigameDefinition::validateInputLayers( const InputMap& inputMap, string_view sourceName ) const
    {
        if ( inputMap.canStackLayers( _listInputContext, sourceName ) )
            return true;
        SW_LOG_ERROR_ID( "MinigameDefinition.InputContextsConflict", "%#: the input contexts cannot be on together - a virtual cursor layer and a layer that binds the same stick or buttons (listed above)", sourceName );
        return false;
    }
} // namespace sw
