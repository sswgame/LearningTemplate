#include "pch.h"

#include "Editor/Panels/Scene/Model/DataLayersModel.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDataLayerSubsystem.h"

namespace sw::editor
{
    DataLayersModel::DataLayersModel()
        : _uniqueHiddenLayer{}
        , _sceneGeneration{ 0 }
    {
    }

    void DataLayersModel::discardIfSceneChanged( const uint64 sceneGeneration )
    {
        if ( _sceneGeneration == sceneGeneration )
            return;
        _sceneGeneration = sceneGeneration;
        _uniqueHiddenLayer.clear();
    }

    void DataLayersModel::collectRows( const Scene& scene, vector<DataLayerRow>& outListRow ) const
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

    void DataLayersModel::setLayerShown( Scene& scene, const string& name, const bool bShown )
    {
        SceneDataLayerSubsystem& layers = scene.getObjectManager()->getSubsystem<SceneDataLayerSubsystem>();
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
        scene.collectObjectsOnDataLayer( name, listObjectID );
        for ( const uint64 objectID : listObjectID )
        {
            GameObject* pObject = scene.getObjectManager()->findGameObjectByID( objectID );
            if ( pObject != nullptr )
                EditorSceneCommands::setHiddenInEditor( pObject, bShown == false );
        }
    }

    void DataLayersModel::setInitiallyActive( Scene& scene, const string& name, const bool bInitiallyActive )
    {
        SceneSettings settings = scene.getSceneSettings();
        for ( SceneDataLayerDef& layer : settings._listDataLayer )
        {
            if ( layer._name == name )
                layer._bInitiallyActive = bInitiallyActive;
        }
        scene.setSceneSettings( settings );
    }
} // namespace sw::editor
