#include "pch.h"

#include "Editor/Panels/Scene/Model/DataLayersViewModel.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneSettingsCommands.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneSettings.h"

namespace sw::editor
{
    DataLayersViewModel::DataLayersViewModel()
        : _uniqueHiddenLayer{}
        , _sceneGeneration{ 0 }
    {
    }

    void DataLayersViewModel::gather( DataLayersViewState& outState )
    {
        outState._listRow.clear();
        outState._content           = DataLayersContent::NoActiveScene;
        Scene* const        pScene  = editor::getActiveScene();
        SceneManager* const pScenes = editor::findSubsystem<SceneManager>();
        if ( pScene == nullptr || pScenes == nullptr || pScene->getObjectManager() == nullptr )
            return;
        if ( _sceneGeneration != pScenes->getSceneGeneration() )
        {
            _sceneGeneration = pScenes->getSceneGeneration();
            _uniqueHiddenLayer.clear();
        }
        collectRows( *pScene, outState._listRow );
        outState._content = outState._listRow.empty() ? DataLayersContent::NoLayers : DataLayersContent::Layers;
    }

    void DataLayersViewModel::collectRows( const Scene& scene, vector<DataLayerRow>& outListRow ) const
    {
        outListRow.clear();
        const SceneDataLayerSubsystem& layers = scene.getObjectManager()->getSubsystem<SceneDataLayerSubsystem>();
        vector<uint64>                 listObjectID;
        for ( const SceneDataLayerDef& layer : scene.getSceneSettings()._listDataLayer )
        {
            scene.collectObjectsOnDataLayer( layer._name, listObjectID );
            DataLayerRow row{};
            row._name             = layer._name;
            row._objectCount      = static_cast<uint32>( listObjectID.size() );
            row._bShown           = layers.isLayerActive( layer._name ) && _uniqueHiddenLayer.find( layer._name ) == _uniqueHiddenLayer.end();
            row._bInitiallyActive = layer._bInitiallyActive;
            outListRow.push_back( std::move( row ) );
        }
    }

    void DataLayersViewModel::setLayerShown( const string& name, const bool bShown )
    {
        Scene* const pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        SceneDataLayerSubsystem& layers = pScene->getObjectManager()->getSubsystem<SceneDataLayerSubsystem>();
        if ( EditorPlaySession::isStopped() == false )
        {
            (void)layers.setLayerActive( name, bShown ); // 선언한 이름만 줄로 그린다
            return;
        }
        if ( bShown )
        {
            _uniqueHiddenLayer.erase( name );
            (void)layers.setLayerActive( name, true );
        }
        else
        {
            _uniqueHiddenLayer.insert( name );
        }
        vector<uint64> listObjectID;
        pScene->collectObjectsOnDataLayer( name, listObjectID );
        for ( const uint64 objectID : listObjectID )
        {
            GameObject* pObject = pScene->getObjectManager()->findGameObjectByID( objectID );
            if ( pObject != nullptr )
                EditorSceneCommands::setHiddenInEditor( pObject, bShown == false );
        }
    }

    void DataLayersViewModel::setLayerInitiallyActive( const string& name, const bool bInitiallyActive )
    {
        Scene* const pScene = editor::getActiveScene();
        if ( pScene == nullptr )
            return;
        SceneSettings settings = pScene->getSceneSettings();
        for ( SceneDataLayerDef& layer : settings._listDataLayer )
        {
            if ( layer._name == name )
                layer._bInitiallyActive = bInitiallyActive;
        }
        (void)EditorSceneSettingsCommands::apply( *pScene, settings, "Set Data Layer On At Load" );
    }
} // namespace sw::editor
