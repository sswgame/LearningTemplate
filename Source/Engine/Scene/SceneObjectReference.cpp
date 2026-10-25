#include "pch.h"

#include "Engine/Scene/SceneObjectReference.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
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

    bool SceneObjectReferenceUtil::findReferenceTo( const SceneManager& sceneManager, const Scene& targetScene, uint64 objectID, string& outHolder )
    {
        outHolder.clear();
        const uint64 fileID = targetScene.findFileID( objectID );
        if ( fileID == 0 || targetScene.getSourcePath().empty() )
            return false;
        static const hashed_string s_referenceType( "SceneObjectReference" );
        for ( const unique_ptr<Scene>& scene : sceneManager.getLoadedScenes() )
        {
            if ( scene == nullptr || scene->getObjectManager() == nullptr )
                continue;
            scene->getObjectManager()->forEachGameObject( [&]( GameObject* pObject )
            {
                for ( Component* pComponent : pObject->getComponents() )
                {
                    const TypeInfo* pType = pComponent != nullptr && pComponent->isPendingDestroy() == false ? pComponent->getTypeInfo() : nullptr;
                    if ( pType == nullptr || outHolder.empty() == false )
                        continue;
                    pType->forEachProperty( [&]( const PropertyInfo& prop )
                    {
                        if ( outHolder.empty() == false || prop._typeName != s_referenceType )
                            return;
                        const SceneObjectReference* pValue          = prop.getValuePtr<SceneObjectReference>( pComponent );
                        const bool                  bPointsAtTarget = pValue != nullptr && pValue->_fileID == fileID && isSameScenePath( pValue->_scenePath, targetScene.getSourcePath() );
                        if ( bPointsAtTarget == false )
                            return;
                        outHolder = scene->getName() + "/" + pObject->getName().c_str() + "." + pType->_name.c_str() + "." + prop._name.c_str();
                    }, true );
                }
            } );
            if ( outHolder.empty() == false )
                return true;
        }
        return false;
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
