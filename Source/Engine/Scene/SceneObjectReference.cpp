#include "pch.h"

#include "Engine/Scene/SceneObjectReference.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    bool SceneObjectReferenceUtil::makeReference( const Scene& scene, uint64 objectID, SceneObjectReference& outReference )
    {
        outReference = SceneObjectReference{};
        if ( scene.getSourcePath().empty() )
            return false;
        const uint64 fileID = scene.getOrAssignFileID( objectID );
        if ( fileID == 0 )
            return false;
        outReference._scenePath = scene.getSourcePath();
        outReference._fileID    = fileID;
        return true;
    }

    SceneObjectResolveResult SceneObjectReferenceUtil::resolve( const SceneManager& sceneManager, const SceneObjectReference& reference, uint64& outObjectID )
    {
        outObjectID = 0;
        if ( reference.isSet() == false )
            return SceneObjectResolveResult::Unset;
        for ( const unique_ptr<Scene>& scene : sceneManager.getLoadedScenes() )
        {
            if ( scene == nullptr || isSameScenePath( scene->getSourcePath(), reference._scenePath ) == false )
                continue;
            return resolveInScene( *scene, reference, outObjectID );
        }
        return SceneObjectResolveResult::SceneNotLoaded;
    }

    SceneObjectResolveResult SceneObjectReferenceUtil::resolveInScene( const Scene& scene, const SceneObjectReference& reference, uint64& outObjectID )
    {
        outObjectID = 0;
        if ( reference.isSet() == false )
            return SceneObjectResolveResult::Unset;
        if ( isSameScenePath( scene.getSourcePath(), reference._scenePath ) == false )
            return SceneObjectResolveResult::SceneNotLoaded;
        outObjectID = scene.findObjectIDByFileID( reference._fileID );
        return outObjectID != 0 ? SceneObjectResolveResult::Resolved : SceneObjectResolveResult::ObjectMissing;
    }

    bool SceneObjectReferenceUtil::isSameScenePath( string_view lhs, string_view rhs )
    {
        if ( lhs.empty() || rhs.empty() )
            return false;
        return StringUtil::toLower( FileUtil::normalizeSeparators( lhs ).c_str() ) == StringUtil::toLower( FileUtil::normalizeSeparators( rhs ).c_str() );
    }
} // namespace sw
