#include "pch.h"

#include "Engine/Scene/SceneDataLayerSubsystem.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

namespace sw
{
    // 짓는 일은 파괴를 처리한 뒤(`DestroyPost`)다 — 스트리밍 셀과 같은 자리다. 만든 오브젝트는 다음 프레임 병합에서 틱에 든다.
    SW_SCENE_SERVICE( SceneDataLayerSubsystem, DestroyPost, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Structure }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    SceneDataLayerSubsystem::SceneDataLayerSubsystem( GameObjectManager& /*manager*/ )
        : _pScene{ nullptr }
        , _settings{}
        , _listGroup{}
        , _uniqueActiveLayer{}
        , _mapObjectGroup{}
        , _layerTags{}
        , _activeSerial{ 0 }
        , _bDirty{ SW_FALSE }
    {
    }

    void SceneDataLayerSubsystem::beginScene( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::SceneObjectNode>& listLayeredNode, const bool bActivateAll )
    {
        _pScene   = &scene;
        _settings = settings;
        scene.recordHeldFileIDs( listLayeredNode ); // 켜지 않은 레이어의 엔티티는 오브젝트가 없다
        _listGroup.clear();
        _uniqueActiveLayer.clear();
        _mapObjectGroup.clear();
        for ( const SceneDataLayerDef& layer : settings._listDataLayer )
        {
            if ( bActivateAll || layer._bInitiallyActive )
                _uniqueActiveLayer.insert( layer._name );
        }
        for ( const SceneDocument::SceneObjectNode& node : listLayeredNode )
        {
            Group* pGroup = nullptr;
            for ( Group& group : _listGroup )
            {
                if ( group._listLayer == node._listDataLayer )
                {
                    pGroup = &group;
                    break;
                }
            }
            if ( pGroup == nullptr )
            {
                pGroup             = &_listGroup.emplace_back();
                pGroup->_listLayer = node._listDataLayer;
            }
            pGroup->_listNode.push_back( node );
        }
        // 대상의 레이어 ⊆ 가리키는 쪽의 레이어이므로(쿠커가 검사한다) 집합이 작은 묶음이 먼저 올라야 한다. 이름 순서는 결정성을 위해서다.
        VectorUtil::sort( _listGroup, []( const Group& lhs, const Group& rhs )
        { return lhs._listLayer.size() != rhs._listLayer.size() ? lhs._listLayer.size() < rhs._listLayer.size() : lhs._listLayer < rhs._listLayer; } );
        _bDirty = SW_FALSE;
        publishActiveLayers();
        applyGroups();
    }

    void SceneDataLayerSubsystem::publishActiveLayers()
    {
        ++_activeSerial;
        vector<string> listActive;
        collectActiveLayers( listActive );
        // 새 몫을 건 뒤 앞 몫이 풀린다 — 그대로 켜진 레이어의 태그는 꺼졌다 켜지지 않는다. '.' 이 든 레이어 이름은 태그가 될 수 없다(`ResourceTags` 가 오류로 남긴다).
        _layerTags = engine::getSubsystem<ResourceTags>().addAxisValues( "layer", listActive );
    }

    void SceneDataLayerSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _bDirty == SW_FALSE || _pScene == nullptr )
            return;
        _bDirty = SW_FALSE;
        applyGroups();
    }

    void SceneDataLayerSubsystem::endScene()
    {
        if ( _pScene == nullptr )
            return;
        _listGroup.clear();
        _uniqueActiveLayer.clear();
        _mapObjectGroup.clear();
        _pScene = nullptr;
        _bDirty = SW_FALSE;
        publishActiveLayers();
    }

    void SceneDataLayerSubsystem::onSceneCleared()
    {
        // 매니저가 오브젝트를 비웠다(세이브 복원) — 묶음 · 켜진 레이어 · 떠 둔 상태는 지키고 지은 표시만 지운다. 다음 갱신이 켜진 묶음을 다시 짓는다(스트리밍 셀과 같다).
        // 씬을 받지 않은 매니저(소멸 · 미리보기)는 지킬 것이 없다.
        for ( Group& group : _listGroup )
        {
            group._listObjectID.clear();
            group._bBuilt  = SW_FALSE;
            group._bFailed = SW_FALSE;
        }
        _mapObjectGroup.clear();
        _bDirty = _pScene != nullptr ? SW_TRUE : SW_FALSE;
    }

    bool SceneDataLayerSubsystem::setLayerActive( string_view name, const bool bActive )
    {
        if ( _settings.findDataLayer( name ) == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneDataLayers.LayerNotDeclared", "Data layer '%#' is not declared by this scene", name );
            return false;
        }
        const string key{ name };
        if ( bActive )
            _uniqueActiveLayer.insert( key );
        else
            _uniqueActiveLayer.erase( key );
        _bDirty = SW_TRUE;
        publishActiveLayers();
        return true;
    }

    bool SceneDataLayerSubsystem::isLayerActive( string_view name ) const
    {
        return _uniqueActiveLayer.find( string{ name } ) != _uniqueActiveLayer.end();
    }

    bool SceneDataLayerSubsystem::areLayersActive( const vector<string>& listLayer ) const
    {
        for ( const string& layer : listLayer )
        {
            if ( _uniqueActiveLayer.find( layer ) == _uniqueActiveLayer.end() )
                return false;
        }
        return true;
    }

    void SceneDataLayerSubsystem::collectActiveLayers( vector<string>& outListLayer ) const
    {
        outListLayer.clear();
        for ( const string& layer : _uniqueActiveLayer )
        {
            outListLayer.push_back( layer );
        }
        VectorUtil::sort( outListLayer );
    }

    SceneDataLayerSubsystem::State SceneDataLayerSubsystem::captureState() const
    {
        State state{};
        state._listGroup = _listGroup;
        collectActiveLayers( state._listActiveLayer );
        return state;
    }

    void SceneDataLayerSubsystem::restoreState( Scene& scene, const SceneSettings& settings, State state )
    {
        _pScene    = &scene;
        _settings  = settings;
        _listGroup = std::move( state._listGroup );
        _uniqueActiveLayer.clear();
        for ( const string& layer : state._listActiveLayer )
        {
            _uniqueActiveLayer.insert( layer );
        }
        rebuildObjectGroupMap();
        _bDirty = SW_FALSE;
        publishActiveLayers();
    }

    void SceneDataLayerSubsystem::rebuildObjectGroupMap()
    {
        _mapObjectGroup.clear();
        for ( uint32 groupIndex = 0; groupIndex < static_cast<uint32>( _listGroup.size() ); ++groupIndex )
        {
            for ( const uint64 objectID : _listGroup[groupIndex]._listObjectID )
            {
                _mapObjectGroup[objectID] = groupIndex;
            }
        }
    }

    bool SceneDataLayerSubsystem::isListedObject( const uint64 objectID ) const
    {
        return _mapObjectGroup.find( objectID ) != _mapObjectGroup.end();
    }

    void SceneDataLayerSubsystem::releaseObject( const uint64 objectID )
    {
        const auto groupIt = _mapObjectGroup.find( objectID );
        if ( groupIt == _mapObjectGroup.end() )
            return;
        (void)VectorUtil::removeValue( _listGroup[groupIt->second]._listObjectID, objectID );
        _mapObjectGroup.erase( groupIt );
    }

    bool SceneDataLayerSubsystem::isGroupObject( const uint64 objectID ) const
    {
        if ( _pScene == nullptr || _mapObjectGroup.empty() )
            return false;
        // 묶음 엔티티에 붙은 자식(중첩 프리팹 부품)은 묶음이 다시 지을 때 함께 선다. 부모가 지워져도 남는 것(탑승자)은 조상의 몫이 아니다.
        for ( const GameObject* pObject = _pScene->getObjectManager()->findGameObjectByID( objectID ); pObject != nullptr; pObject = pObject->isKeptWhenParentDestroyed() ? nullptr : pObject->getParent() )
        {
            if ( _mapObjectGroup.find( pObject->getObjectID() ) != _mapObjectGroup.end() )
                return true;
        }
        return false;
    }

    void SceneDataLayerSubsystem::writeSaveState( vector<uint8>& outBytes ) const
    {
        Archive        arch;
        vector<string> listActive;
        if ( _pScene != nullptr )
            collectActiveLayers( listActive );
        arch << static_cast<uint32>( listActive.size() );
        for ( const string& layer : listActive )
        {
            arch << string_view( layer );
        }
        const uint32 groupCount = _pScene != nullptr ? static_cast<uint32>( _listGroup.size() ) : 0u;
        arch << groupCount;
        for ( uint32 groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const Group& group = _listGroup[groupIndex];
            arch << static_cast<uint32>( group._listLayer.size() );
            for ( const string& layer : group._listLayer )
            {
                arch << string_view( layer );
            }
            // 지은 묶음은 지금 떠서 싣는다(그동안 바뀌었을 수 있다). 내려간 묶음은 떠 둔 상태가 있으면 그것을, 없으면 문서 그대로라 싣지 않는다.
            vector<SceneDocument::SceneObjectNode> listCaptured;
            if ( group._bBuilt )
                _pScene->captureCellNodes( group._listObjectID, listCaptured );
            const bool    bHeld = group._bBuilt || group._bStateHeld;
            vector<uint8> bytes;
            SceneDocument::writeSavedNodes( group._bBuilt ? listCaptured : ( bHeld ? group._listHeldNode : vector<SceneDocument::SceneObjectNode>{} ), bytes );
            arch << static_cast<uint8>( bHeld ? 1u : 0u );
            arch.writeSection( bytes.data(), static_cast<uint32>( bytes.size() ) );
        }
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    bool SceneDataLayerSubsystem::readSaveState( const uint8* pData, const size_t size, SaveState& outState )
    {
        outState = SaveState{};
        if ( pData == nullptr )
            return false;
        Archive arch( pData, size );
        uint32  activeCount{ 0 };
        activeCount = arch.readCount( sizeof( uint32 ) );
        if ( arch.isError() )
            return false;
        outState._listActiveLayer.resize( activeCount );
        for ( string& layer : outState._listActiveLayer )
        {
            arch >> layer;
        }
        uint32 groupCount{ 0 };
        groupCount = arch.readCount( sizeof( uint32 ) * 2 + sizeof( uint8 ) );
        if ( arch.isError() )
            return false;
        outState._listGroup.resize( groupCount );
        for ( SavedGroup& group : outState._listGroup )
        {
            uint32 layerCount{ 0 };
            layerCount = arch.readCount( sizeof( uint32 ) );
            if ( arch.isError() )
                return false;
            group._listLayer.resize( layerCount );
            for ( string& layer : group._listLayer )
            {
                arch >> layer;
            }
            uint8 held{ 0 };
            arch >> held;
            group._bStateHeld = held != 0;
            vector<uint8> bytes;
            if ( arch.readSection( bytes ) == false || arch.isError() ||
                 SceneDocument::readSavedNodes( bytes.data(), bytes.size(), group._listHeldNode, "save game (data layers)" ) == false )
            {
                SW_LOG_ERROR_ID( "SceneDataLayers.SaveGroupsCorrupted", "Save game data layer groups could not be read - the save is not used" );
                outState = SaveState{};
                return false;
            }
        }
        return arch.isError() == false;
    }

    void SceneDataLayerSubsystem::applySaveState( SaveState state )
    {
        if ( _pScene == nullptr )
        {
            if ( state._listActiveLayer.empty() == false || state._listGroup.empty() == false )
                SW_LOG_ERROR_ID( "SceneDataLayers.SaveWithoutLayers", "The save carries data layer state but the active scene has no data layers - it is not restored" );
            return;
        }
        _uniqueActiveLayer.clear();
        for ( const string& layer : state._listActiveLayer )
        {
            if ( _settings.findDataLayer( layer ) == nullptr )
            {
                SW_LOG_ERROR_ID( "SceneDataLayers.SaveLayerNotDeclared", "The save turns on data layer '%#' that this scene does not declare - it stays off", layer );
                continue;
            }
            _uniqueActiveLayer.insert( layer );
        }
        for ( SavedGroup& saved : state._listGroup )
        {
            Group* pGroup = nullptr;
            for ( Group& group : _listGroup )
            {
                pGroup = pGroup == nullptr && group._listLayer == saved._listLayer ? &group : pGroup;
            }
            if ( pGroup == nullptr )
            {
                SW_LOG_ERROR_ID( "SceneDataLayers.SaveGroupUnknown", "The save carries a data layer group ('%#' first) this scene does not have - its entities are not restored",
                                 saved._listLayer.empty() ? string{} : saved._listLayer[0] );
                continue;
            }
            if ( pGroup->_bBuilt )
            {
                _pScene->unloadCell( pGroup->_listObjectID ); // 복원은 비운 매니저 위다 — 남은 오브젝트가 있으면 세이브의 상태로 갈아 끼운다
                pGroup->_listObjectID.clear();
                pGroup->_bBuilt = SW_FALSE;
            }
            _pScene->recordHeldFileIDs( saved._listHeldNode );
            pGroup->_listHeldNode = std::move( saved._listHeldNode );
            pGroup->_bStateHeld   = saved._bStateHeld;
            pGroup->_bFailed      = SW_FALSE;
        }
        rebuildObjectGroupMap();
        _bDirty = SW_TRUE;
        publishActiveLayers();
    }

    void SceneDataLayerSubsystem::applyGroups()
    {
        for ( size_t index = _listGroup.size(); index > 0; --index )
        {
            Group& group = _listGroup[index - 1];
            if ( group._bBuilt == SW_FALSE || areLayersActive( group._listLayer ) )
                continue;
            // 다시 켤 때 이 상태로 짓는다 — 내리기 전에 뜬다(런타임 칸까지, 스트리밍 셀과 같다). 문서(`_listNode`)는 저장이 다시 쓰도록 그대로 둔다.
            _pScene->captureNodesForUnload( group._listObjectID, group._listHeldNode );
            group._bStateHeld = SW_TRUE;
            for ( const uint64 objectID : group._listObjectID )
            {
                _mapObjectGroup.erase( objectID );
            }
            _pScene->unloadCell( group._listObjectID );
            group._listObjectID.clear();
            group._bBuilt = SW_FALSE;
        }
        for ( Group& group : _listGroup )
        {
            const bool bStable = group._bBuilt || group._bFailed || areLayersActive( group._listLayer ) == false;
            if ( bStable )
                continue;
            group._listObjectID.clear();
            if ( _pScene->instantiateLayerNodes( group._bStateHeld ? group._listHeldNode : group._listNode, group._listObjectID ) )
            {
                group._bBuilt = SW_TRUE;
                for ( const uint64 objectID : group._listObjectID )
                {
                    _mapObjectGroup[objectID] = static_cast<uint32>( &group - _listGroup.data() );
                }
                continue;
            }
            SW_LOG_ERROR_ID( "SceneDataLayers.GroupNotBuilt", "Scene '%#' could not load the entities of a data layer group ('%#' first) - they stay unloaded", _pScene->getName(), group._listLayer[0] );
            // 반쯤 지은 것을 남기지 않는다.
            _pScene->unloadCell( group._listObjectID );
            group._listObjectID.clear();
            group._bFailed = SW_TRUE;
        }
    }
} // namespace sw
