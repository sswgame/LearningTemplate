#include "pch.h"

#include "Engine/Scene/SceneDataLayerSubsystem.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
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
        , _bDirty{ SW_FALSE }
    {
    }

    void SceneDataLayerSubsystem::beginScene( Scene& scene, const SceneSettings& settings, const vector<SceneDocument::SceneObjectNode>& listLayeredNode, const bool bActivateAll )
    {
        _pScene   = &scene;
        _settings = settings;
        _listGroup.clear();
        _uniqueActiveLayer.clear();
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
        publishLayerTags();
        applyGroups();
    }

    void SceneDataLayerSubsystem::publishLayerTags() const
    {
        vector<string> listActive;
        collectActiveLayers( listActive );
        (void)engine::getSubsystem<ResourceTags>().setAxisValues( "layer", listActive ); // '.' 이 든 레이어 이름은 태그가 될 수 없다 — `ResourceTags` 가 오류로 남긴다
    }

    void SceneDataLayerSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _bDirty == SW_FALSE || _pScene == nullptr )
            return;
        _bDirty = SW_FALSE;
        applyGroups();
    }

    void SceneDataLayerSubsystem::onSceneCleared()
    {
        _listGroup.clear();
        _uniqueActiveLayer.clear();
        _pScene = nullptr;
        _bDirty = SW_FALSE;
        publishLayerTags();
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
        publishLayerTags();
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
        _bDirty = SW_FALSE;
        publishLayerTags();
    }

    void SceneDataLayerSubsystem::applyGroups()
    {
        for ( size_t index = _listGroup.size(); index > 0; --index )
        {
            Group& group = _listGroup[index - 1];
            if ( group._bBuilt == SW_FALSE || areLayersActive( group._listLayer ) )
                continue;
            _pScene->unloadCell( group._listObjectID );
            group._listObjectID.clear();
            group._bBuilt = SW_FALSE;
        }
        for ( Group& group : _listGroup )
        {
            const bool bSettled = group._bBuilt || group._bFailed || areLayersActive( group._listLayer ) == false;
            if ( bSettled )
                continue;
            group._listObjectID.clear();
            if ( _pScene->instantiateLayerNodes( group._listNode, group._listObjectID ) )
            {
                group._bBuilt = SW_TRUE;
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
