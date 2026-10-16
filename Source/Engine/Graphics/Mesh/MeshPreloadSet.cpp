#include "pch.h"

#include "Engine/Graphics/Mesh/MeshPreloadSet.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshCache.h"

namespace sw
{
    MeshPreloadSet::~MeshPreloadSet()
    {
        clear();
    }

    void MeshPreloadSet::request( vector<string> listPath )
    {
        clear();
        _listPath = std::move( listPath );
        if ( _listPath.empty() )
            return;
        if ( engine::areEngineServicesBound() == false )
        {
            loadAll( TaskArgs{} );
            return;
        }
        TaskManager& taskManager = engine::getSubsystem<TaskManager>();
        _stage                   = taskManager.createStage( "MeshPreload" );
        TaskHandle handle        = taskManager.emplaceTask( "MeshPreload", SW_DELEGATE_METHOD( TaskArgsDelegate, &MeshPreloadSet::loadAll, this ), TaskArgs{} );
        handle.setPriority( TaskPriority::Low ); // 백그라운드 IO 큐에서 돌아 프레임 작업을 막지 않는다(AssetStreamingQueue 와 같다)
        _stage.addTask( handle );
        handle.submit();
    }

    void MeshPreloadSet::clear()
    {
        if ( _stage.isValid() )
        {
            engine::getSubsystem<TaskManager>().waitStage( _stage );
            _stage = TaskStageHandle{};
        }
        _listMesh.clear();
        _listPath.clear();
    }

    bool MeshPreloadSet::isComplete() const
    {
        return _stage.isValid() == false || engine::getSubsystem<TaskManager>().isStageComplete( _stage );
    }

    size_t MeshPreloadSet::getLoadedCount() const
    {
        return _listMesh.size();
    }

    void MeshPreloadSet::loadAll( [[maybe_unused]] const TaskArgs& args )
    {
        _listMesh.reserve( _listPath.size() );
        for ( const string& path : _listPath )
        {
            shared_ptr<Mesh> mesh = MeshCache::acquire( path );
            if ( mesh != nullptr )
                _listMesh.push_back( std::move( mesh ) );
        }
    }
} // namespace sw
