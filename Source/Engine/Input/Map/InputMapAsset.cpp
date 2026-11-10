#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/Input/GamepadButtonUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputMapParams.h"
#include "Engine/Input/Map/InputMapXML.h"
#include "Engine/Input/Virtual/VirtualCursor.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

/**
 * @file InputMapAsset.cpp
 * @brief InputMap XML 정의 · 묶음의 로드와 저장을 구현합니다.
 * @details 파일 나눔은 InputMap.cpp 머리말을 본다.
 */

namespace sw
{
    bool InputMap::loadFromAsset( const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        return loadDefinition( AssetID{ relativePath }, false, true );
    }

    bool InputMap::appendFromAsset( const AssetID& assetID )
    {
        return loadDefinition( assetID, true, true );
    }

    bool InputMap::loadBundle( const vector<AssetID>& listAsset )
    {
        if ( listAsset.empty() )
        {
            SW_LOG_ERROR_ID( "InputMap.BundleEmpty", "InputMap: a bundle needs at least its base map" );
            clear();
            return false;
        }
        bool bValid = true;
        for ( size_t index = 0; index < listAsset.size() && bValid; ++index )
        {
            bValid = loadDefinition( listAsset[index], index > 0, false );
        }
        // 뒤 파일의 `virtualCursor` 는 앞 파일의 바인딩과도 겹칠 수 있고, 앞 파일끼리의 겹침은 뒤 파일이 풀 수 있다 — 검사는 다 읽은 맵에 한 번 한다.
        const string_view sourceName = listAsset.back().view();
        if ( bValid )
            bValid = countVirtualCursorConflicts( sourceName ) == 0 && countAllSameLayerInputConflicts( sourceName ) == 0;
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.BundleRejected", "InputMap: the bundle ending in %# was rejected (listed above) - no actions are bound", sourceName );
            clear();
        }
        return bValid;
    }

    bool InputMap::loadDefinition( const AssetID& assetID, bool bAppend, bool bCheckConflicts )
    {
        const string_view relativePath = assetID.view();
        XMLDocument       doc;
        string            absPath;
        if ( doc.loadResource( assetID, &absPath ) == false )
        {
            SW_LOG_WARNING_ID( "InputMap.FailedLoadInputMap", "Failed to load InputMap %#", relativePath );
            return false;
        }

        XMLNode root = doc.getRoot( "InputMap" );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING_ID( "InputMap.MissingInputMap", "Missing <InputMap> in %#", absPath );
            return false;
        }

        // 읽기 단계: 맵 전체 값 → 레이어 → 액션 → 충돌 검사. 단계가 실패하면 거기서 끝낸다.
        // 더하는 파일은 맵 전체 값을 정하지 않는다 — 두 파일이 각자 정하면 어느 값이 이기는지가 읽는 순서에 달린다.
        // `virtualCursor` 만 받는다. 시간 값과 달리 게임마다 다른 기능이고, 기본 맵을 둘로 두면 시간 값이 두 벌이 된다. 두 번 적으면 `readMapSettings` 가 거절한다.
        for ( XMLAttribute attribute = root.getFirstAttribute(); bAppend && attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( string_view{ attribute.getName() } == "virtualCursor" )
                continue;
            SW_LOG_ERROR_ID( "InputMap.InputMapAppendedAnotherMap", "InputMap %# is appended to another map but sets map-wide attributes - only the base map sets them", absPath );
            return false;
        }
        if ( readMapSettings( root, absPath, bAppend ) == false )
            return false;
        // 맵을 비운(`readMapSettings`) 뒤에 올린다 — 이 파일의 레이어 · 액션이 이 번호를 받는다.
        _listSource.emplace_back( relativePath, bAppend == false, root.hasAttribute( "virtualCursor" ) );
        _currentSourceIndex = static_cast<uint32>( _listSource.size() - 1 );

        bool bValid = true;
        readLayerDefinitions( root, absPath, bAppend, bValid );
        vector<hashed_string> listLoadedAction;
        readActionDefinitions( root, absPath, bAppend, listLoadedAction, bValid );

        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapInvalidAttributeValues", "InputMap %# has invalid attribute values (listed above)", absPath );
            return false;
        }
        if ( bCheckConflicts == false )
            return true;
        // 가상 커서가 마우스로 바꾸는 스틱 · 버튼을 액션에 따로 묶으면 한 입력이 두 입력이 된다 — 읽기를 실패로 돌려 데이터를 고치게 한다.
        if ( countVirtualCursorConflicts( absPath ) > 0 )
            return false;
        uint32 sameLayerConflictCount = 0;
        for ( const hashed_string& actionName : listLoadedAction )
        {
            sameLayerConflictCount += countSameLayerInputConflicts( absPath, actionName );
        }
        return sameLayerConflictCount == 0;
    }

    bool InputMap::readMapSettings( const XMLNode& root, string_view absPath, bool bAppend )
    {
        static constexpr const utf8* kArrRootChild[] = { "layers", "layer", "action", "override", "remove" };
        InputMapRootParams           params;
        const bool                   bFields   = XMLFields::readFields( root, params, absPath );
        const bool                   bChildren = XMLNameCheck::reportUnknownChildren( root, kArrRootChild, absPath );
        if ( bFields == false || bChildren == false )
            return false;
        if ( bAppend )
        {
            if ( params._virtualCursor == InputMapVirtualCursor::Off )
                return true;
            if ( _virtualCursorStick != InputMapVirtualCursor::Off )
            {
                SW_LOG_ERROR_ID( "InputMap.VirtualCursorSetTwice", "InputMap %# sets virtualCursor but an earlier map of the bundle already did", absPath );
                return false;
            }
            _virtualCursorStick = params._virtualCursor;
            return true;
        }
        clear();
        const bool bTimingRefused = setDoubleClickTime( params._doubleClickTime ) == false || setDoubleClickMaxDistance( params._doubleClickMaxDistance ) == false ||
                                    setHoldThreshold( params._holdThreshold ) == false || setDragThreshold( params._dragThreshold ) == false;
        if ( bTimingRefused )
            return false;
        _virtualCursorStick = params._virtualCursor;
        _defaultLayerName   = params._defaultLayer;
        return true;
    }

    bool InputMap::readLayer( const XMLNode& layerNode, string_view absPath, bool bAppend, std::initializer_list<string_view> listChild, hashed_string& outName )
    {
        InputMapLayerParams params;
        const bool          bFields   = XMLFields::readFields( layerNode, params, absPath );
        bool                bChildren = true;
        for ( XMLNode child = layerNode.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( VectorUtil::contains( listChild, string_view{ child.getName() } ) )
                continue;
            SW_LOG_ERROR_ID( "InputMap.LayerUnknownElement", "InputMap %#: <layer name=\"%#\"> has unknown element <%#>", absPath, params._name.c_str(), child.getName() );
            bChildren = false;
        }
        if ( bFields == false || bChildren == false )
            return false;
        outName = params._name;
        if ( bAppend && hasLayer( params._name ) && listChild.size() == 0 )
        {
            SW_LOG_ERROR_ID( "InputMap.InputMapRedefinesLayerMap", "InputMap %# redefines layer '%#' that the map already has - put actions in it with <layer name=...> instead", absPath, params._name.c_str() );
            return false;
        }
        if ( hasLayer( params._name ) == false || listChild.size() == 0 )
            registerLayer( params._name, params._priority, params._bEnabled, params._bBlockLower, params._bAlwaysOn );
        return true;
    }

    void InputMap::readLayerDefinitions( const XMLNode& root, string_view absPath, bool bAppend, bool& inoutbValid )
    {
        const XMLNode layersNode = root.findChild( "layers" );
        for ( XMLNode layerNode = layersNode.findChild(); layerNode.isValid(); layerNode = layerNode.findNextSibling() )
        {
            hashed_string name;
            if ( string_view{ layerNode.getName() } != "layer" )
            {
                SW_LOG_ERROR_ID( "InputMap.LayersHoldsOnlyLayer", "InputMap %#: <layers> holds only <layer> elements (found <%#>)", absPath, layerNode.getName() );
                inoutbValid = false;
                continue;
            }
            inoutbValid = readLayer( layerNode, absPath, bAppend, {}, name ) && inoutbValid;
        }
        ensureLayer( _defaultLayerName, 0, true, false );
    }

    void InputMap::readActionDefinitions( const XMLNode& root, string_view absPath, bool bAppend, vector<hashed_string>& outListLoadedAction, bool& inoutbValid )
    {
        // 더할 때는 이미 있는 액션을 다시 정의하지 못한다(두 파일이 같은 이름을 쓰면 바인딩이 섞인다). 더한 액션은 끝에서 같은 레이어 충돌을 본다.
        const auto loadAction = [&]( const XMLNode& actionNode, const hashed_string& inheritedLayer )
        {
            const hashed_string actionName( actionNode.getAttributeText( "name" ) );
            if ( bAppend && actionName.empty() == false )
            {
                if ( hasAction( actionName ) )
                {
                    SW_LOG_ERROR_ID( "InputMap.InputMapDefinesActionMap", "InputMap %# defines action '%#' that the map already has", absPath, actionName.c_str() );
                    inoutbValid = false;
                    return;
                }
                outListLoadedAction.push_back( actionName );
            }
            InputMapXML::readAction( *this, actionNode, inheritedLayer, absPath, inoutbValid );
        };

        for ( XMLNode layerNode = root.findChild( "layer" ); layerNode.isValid(); layerNode = layerNode.findNextSibling( "layer" ) )
        {
            hashed_string layerName;
            if ( readLayer( layerNode, absPath, bAppend, { "action" }, layerName ) == false )
            {
                inoutbValid = false;
                continue;
            }
            for ( XMLNode actionNode = layerNode.findChild( "action" ); actionNode.isValid(); actionNode = actionNode.findNextSibling( "action" ) )
            {
                loadAction( actionNode, layerName );
            }
        }

        for ( XMLNode actionNode = root.findChild( "action" ); actionNode.isValid(); actionNode = actionNode.findNextSibling( "action" ) )
        {
            loadAction( actionNode, _defaultLayerName );
        }

        // 차이는 앞 파일이 있어야 뜻이 있다. 기본 맵에 적은 것은 틀린 파일이다.
        if ( bAppend == false && ( root.findChild( "override" ).isValid() || root.findChild( "remove" ).isValid() ) )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffInBaseMap", "InputMap %# is a base map but has <override> or <remove> - only a later map of a bundle can", absPath );
            inoutbValid = false;
            return;
        }
        for ( XMLNode node = root.findChild( "override" ); node.isValid(); node = node.findNextSibling( "override" ) )
        {
            readOverride( node, absPath, outListLoadedAction, inoutbValid );
        }
        for ( XMLNode node = root.findChild( "remove" ); node.isValid(); node = node.findNextSibling( "remove" ) )
        {
            readRemove( node, absPath, inoutbValid );
        }
    }

    InputMap::ActionEntry* InputMap::findDiffTarget( const XMLNode& node, const hashed_string& action, string_view absPath )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffUnknownAction", "InputMap %#: <%#> names action '%#' that no earlier map of the bundle defines", absPath, node.getName(), action.c_str() );
            return nullptr;
        }
        if ( pEntry->_bOverride == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffTwice", "InputMap %#: <%#> names action '%#' that %# already overrode - one diff per action, or the winner depends on the load order", absPath, node.getName(),
                             action.c_str(), _listSource[pEntry->_sourceIndex]._assetPath.c_str() );
            return nullptr;
        }
        if ( pEntry->_sourceIndex == _currentSourceIndex )
        {
            SW_LOG_ERROR_ID( "InputMap.DiffSameFile", "InputMap %#: <%#> names action '%#' that the same file defines - edit the definition instead", absPath, node.getName(), action.c_str() );
            return nullptr;
        }
        return pEntry;
    }

    void InputMap::shadowActionDefinition( const ActionEntry& entry, const hashed_string& action )
    {
        SourceActionSnapshot snapshot;
        snapshot._action      = action;
        snapshot._valueType   = entry._valueType;
        snapshot._listBinding = entry._listBinding;
        _listSource[entry._sourceIndex]._listShadowed.push_back( std::move( snapshot ) );
    }

    void InputMap::readOverride( const XMLNode& node, string_view absPath, vector<hashed_string>& outListLoadedAction, bool& inoutbValid )
    {
        const hashed_string action( node.getAttributeText( "name" ) );
        ActionEntry* const  pEntry = findDiffTarget( node, action, absPath );
        if ( pEntry == nullptr )
        {
            inoutbValid = false;
            return;
        }
        shadowActionDefinition( *pEntry, action );
        // `layer` 를 적지 않은 덮기는 덮는 액션의 레이어에 남는다 — 차이는 키만 적으면 된다.
        const hashed_string inheritedLayer = pEntry->_listBinding.empty() ? _defaultLayerName : pEntry->_listBinding.front()._layer;
        pEntry->_listBinding.clear();
        pEntry->_listDefaultBinding.clear();
        pEntry->_listBindingState.clear();
        pEntry->_sourceIndex = _currentSourceIndex;
        pEntry->_bOverride   = SW_TRUE;
        outListLoadedAction.push_back( action );
        InputMapXML::readAction( *this, node, inheritedLayer, absPath, inoutbValid );
    }

    void InputMap::readRemove( const XMLNode& node, string_view absPath, bool& inoutbValid )
    {
        InputMapRemoveParams params;
        const bool           bFields   = XMLFields::readFields( node, params, absPath );
        const bool           bChildren = XMLNameCheck::reportUnknownChildren( node, nullptr, 0, absPath );
        if ( bFields == false || bChildren == false )
        {
            inoutbValid = false;
            return;
        }
        ActionEntry* const pEntry = findDiffTarget( node, params._name, absPath );
        if ( pEntry == nullptr )
        {
            inoutbValid = false;
            return;
        }
        shadowActionDefinition( *pEntry, params._name );
        (void)removeAction( params._name ); // 바로 위에서 찾았다
        _listSource[_currentSourceIndex]._listRemove.push_back( params._name );
    }

    uint32 InputMap::countAllSameLayerInputConflicts( string_view sourceName ) const
    {
        uint32                conflictCount = 0;
        vector<hashed_string> listVisited;
        for ( const hashed_string& action : _listActionName )
        {
            const uint32 bindCount = getBindingCount( action );
            for ( uint32 bindIndex = 0; bindIndex < bindCount; ++bindIndex )
            {
                hashed_string other{};
                if ( findSameLayerInputConflict( action, bindIndex, other ) == false || VectorUtil::contains( listVisited, other ) )
                    continue; // 짝의 앞쪽에서 이미 알렸다
                SW_LOG_ERROR_ID( "InputMap.InputMapActionUsesSame", "InputMap %#: action '%#' uses the same input as '%#' in layer '%#' - one press would fire both", sourceName, action.c_str(), other.c_str(),
                                 findBindingLayer( action, bindIndex ).c_str() );
                ++conflictCount;
            }
            listVisited.push_back( action );
        }
        return conflictCount;
    }

    uint32 InputMap::countSameLayerInputConflicts( string_view sourceName, const hashed_string& action ) const
    {
        const ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0;
        uint32 conflictCount = 0;
        for ( uint32 bindIndex = 0; bindIndex < static_cast<uint32>( pEntry->_listBinding.size() ); ++bindIndex )
        {
            hashed_string otherName{};
            if ( findSameLayerInputConflict( action, bindIndex, otherName ) == false )
                continue;
            SW_LOG_ERROR_ID( "InputMap.InputMapActionUsesSame", "InputMap %#: action '%#' uses the same input as '%#' in layer '%#' - one press would fire both", sourceName, action.c_str(), otherName.c_str(),
                             pEntry->_listBinding[bindIndex]._layer.c_str() );
            ++conflictCount;
        }
        return conflictCount;
    }

    uint32 InputMap::countVirtualCursorConflicts( string_view sourceName ) const
    {
        uint32 conflictCount = 0;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry == nullptr )
                continue;
            for ( const ActionBinding& binding : pEntry->_listBinding )
            {
                if ( reportVirtualCursorConflict( sourceName, actionName, binding ) )
                    ++conflictCount;
            }
        }
        return conflictCount;
    }

    uint32 InputMap::removeVirtualCursorConflicts( string_view sourceName, const hashed_string& action, uint32 firstIndex )
    {
        ActionEntry* pEntry = findAction( action );
        if ( pEntry == nullptr )
            return 0;
        uint32 removedCount = 0;
        for ( uint32 bindIndex = static_cast<uint32>( pEntry->_listBinding.size() ); bindIndex > firstIndex; --bindIndex )
        {
            const uint32 removeIndex = bindIndex - 1;
            if ( reportVirtualCursorConflict( sourceName, action, pEntry->_listBinding[removeIndex] ) == false )
                continue;
            pEntry->_listBinding.erase( pEntry->_listBinding.begin() + removeIndex );
            pEntry->_listDefaultBinding.erase( pEntry->_listDefaultBinding.begin() + removeIndex );
            pEntry->_listBindingState.erase( pEntry->_listBindingState.begin() + removeIndex );
            ++removedCount;
        }
        return removedCount;
    }

    bool InputMap::reportVirtualCursorConflict( string_view sourceName, const hashed_string& action, const ActionBinding& binding ) const
    {
        if ( _virtualCursorStick == InputMapVirtualCursor::Off )
            return false;
        const VirtualCursorSettings cursor{};
        const GamepadStick          cursorStick       = _virtualCursorStick == InputMapVirtualCursor::RightStick ? GamepadStick::Right : GamepadStick::Left;
        const InputSlot             arrCursorButton[] = { InputSlot::fromGamepadButton( cursor._primaryButton, cursor._padIndex ),
                                                          InputSlot::fromGamepadButton( cursor._secondaryButton, cursor._padIndex ) };
        const bool                  bCursorStick      = binding._kind == BindingKind::GamepadStick2D && binding._stick == cursorStick && binding._deviceIndex == cursor._padIndex;
        bool                        bCursorButton     = false;
        for ( const InputSlot& slot : binding._arrSlot )
        {
            for ( const InputSlot& button : arrCursorButton )
            {
                bCursorButton = bCursorButton || slot == button;
            }
        }
        if ( bCursorStick == false && bCursorButton == false )
            return false;
        SW_LOG_ERROR_ID( "InputMap.InputMapActionBindsVirtual", "InputMap %#: action '%#' binds the %# the virtual cursor (%#) turns into the mouse - one press would fire twice; bind the mouse only",
                         sourceName, action.c_str(), bCursorStick ? "stick" : "gamepad button", getEnumName( _virtualCursorStick ) );
        return true;
    }

    bool InputMap::saveToAsset( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;

        XMLDocument        doc;
        const XMLNode      root = doc.appendRoot( "InputMap" );
        InputMapRootParams rootParams;
        rootParams._defaultLayer           = _defaultLayerName;
        rootParams._doubleClickTime        = _triggerRules._doubleClickTime;
        rootParams._doubleClickMaxDistance = _triggerRules._doubleClickMaxDistance;
        rootParams._holdThreshold          = _triggerRules._holdThreshold;
        rootParams._dragThreshold          = _triggerRules._dragThreshold;
        rootParams._virtualCursor          = _virtualCursorStick;
        XMLFields::writeFields( root, rootParams );

        const XMLNode layersNode = root.appendChild( "layers" );
        for ( const hashed_string& layerName : _listLayerName )
        {
            writeLayerDef( layersNode, layerName );
        }

        // 액션은 등록 순서대로 쓴다. 그래야 다시 읽은 맵의 액션 순서가 같다.
        bool bAllWritten = true;
        for ( const hashed_string& actionName : _listActionName )
        {
            bAllWritten = InputMapXML::writeAction( root, *this, actionName ) && bAllWritten;
        }
        if ( bAllWritten == false )
            return false;
        if ( doc.saveToFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "InputMap.CouldNotWriteInputMap", "Could not write InputMap %#", absPath.str() );
            return false;
        }
        return true;
    }

    void InputMap::writeLayerDef( const XMLNode& layersNode, const hashed_string& layerName ) const
    {
        const LayerDef* pLayer = findLayer( layerName );
        if ( pLayer == nullptr )
            return;
        InputMapLayerParams layerParams;
        layerParams._name        = layerName;
        layerParams._priority    = pLayer->_priority;
        layerParams._bEnabled    = pLayer->_bEnabled != SW_FALSE;
        layerParams._bBlockLower = pLayer->_bBlockLower != SW_FALSE;
        layerParams._bAlwaysOn   = pLayer->_bAlwaysOn != SW_FALSE;
        XMLFields::writeFields( layersNode.appendChild( "layer" ), layerParams );
    }

    bool InputMap::fillSourceDocument( uint32 sourceIndex, XMLDocument& outDoc ) const
    {
        const SourceRecord& source = _listSource[sourceIndex];
        const XMLNode       root   = outDoc.appendRoot( "InputMap" );
        if ( source._bBase == SW_TRUE )
        {
            InputMapRootParams rootParams;
            rootParams._defaultLayer           = _defaultLayerName;
            rootParams._doubleClickTime        = _triggerRules._doubleClickTime;
            rootParams._doubleClickMaxDistance = _triggerRules._doubleClickMaxDistance;
            rootParams._holdThreshold          = _triggerRules._holdThreshold;
            rootParams._dragThreshold          = _triggerRules._dragThreshold;
            rootParams._virtualCursor          = source._bVirtualCursor == SW_TRUE ? _virtualCursorStick : InputMapVirtualCursor::Off;
            XMLFields::writeFields( root, rootParams );
        }
        else if ( source._bVirtualCursor == SW_TRUE )
        {
            root.appendAttribute( "virtualCursor", getEnumName( _virtualCursorStick ) );
        }

        vector<hashed_string> listOwnLayer;
        for ( const hashed_string& layerName : _listLayerName )
        {
            const LayerDef* pLayer = findLayer( layerName );
            if ( pLayer != nullptr && pLayer->_sourceIndex == sourceIndex )
                listOwnLayer.push_back( layerName );
        }
        if ( listOwnLayer.empty() == false )
        {
            const XMLNode layersNode = root.appendChild( "layers" );
            for ( const hashed_string& layerName : listOwnLayer )
            {
                writeLayerDef( layersNode, layerName );
            }
        }

        // 원래 차례대로 — 자기 정의(지금 맵에 있는 것, 뒤 파일이 덮거나 지운 것), 덮기, 지우기.
        bool bAllWritten = true;
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry != nullptr && pEntry->_sourceIndex == sourceIndex && pEntry->_bOverride == SW_FALSE )
                bAllWritten = InputMapXML::writeActionElement( root, "action", actionName, pEntry->_valueType, pEntry->_listBinding ) && bAllWritten;
        }
        for ( const SourceActionSnapshot& snapshot : source._listShadowed )
        {
            bAllWritten = InputMapXML::writeActionElement( root, "action", snapshot._action, snapshot._valueType, snapshot._listBinding ) && bAllWritten;
        }
        for ( const hashed_string& actionName : _listActionName )
        {
            const ActionEntry* pEntry = findAction( actionName );
            if ( pEntry != nullptr && pEntry->_sourceIndex == sourceIndex && pEntry->_bOverride == SW_TRUE )
                bAllWritten = InputMapXML::writeActionElement( root, "override", actionName, pEntry->_valueType, pEntry->_listBinding ) && bAllWritten;
        }
        for ( const hashed_string& actionName : source._listRemove )
        {
            InputMapRemoveParams removeFields;
            removeFields._name = actionName;
            XMLFields::writeFields( root.appendChild( "remove" ), removeFields );
        }
        return bAllWritten;
    }

    bool InputMap::writeSourceText( uint32 sourceIndex, string& outText ) const
    {
        XMLDocument doc;
        if ( fillSourceDocument( sourceIndex, doc ) == false )
            return false;
        outText = doc.saveToString();
        return true;
    }

    bool InputMap::saveSourceToAsset( uint32 sourceIndex, const AbsolutePath& absPath ) const
    {
        XMLDocument doc;
        if ( fillSourceDocument( sourceIndex, doc ) == false )
            return false;
        if ( doc.saveToFile( absPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "InputMap.CouldNotWriteInputMap", "Could not write InputMap %#", absPath.str() );
            return false;
        }
        return true;
    }
} // namespace sw
