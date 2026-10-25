#include "pch.h"

#include "Engine/Scene/SceneObjectReference.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <algorithm>

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

    bool SceneObjectReferenceUtil::isReferenceTypeName( const hashed_string& typeName )
    {
        static const hashed_string s_referenceType( "SceneObjectReference" );
        return typeName == s_referenceType;
    }

    namespace
    {
        /** @brief 컨테이너(중첩 포함)의 원소 · 맵 키와 값 중 `SceneObjectReference` 를 모읍니다. 컨테이너를 고치면 포인터가 무효가 됩니다. */
        void collectContainerReferences( const void* pContainer, const NestedContainerInfo& shape, vector<const SceneObjectReference*>& outListReference )
        {
            if ( pContainer == nullptr || shape._wrapper == nullptr )
                return;
            if ( ISequenceContainerWrapper* pSequence = shape._wrapper->asSequence() )
            {
                const size_t elementCount = pSequence->getSize( pContainer );
                for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                {
                    const void* pElement = pSequence->getElementConst( pContainer, elementIndex );
                    if ( shape._elementNested != nullptr )
                        collectContainerReferences( pElement, *shape._elementNested, outListReference );
                    else if ( SceneObjectReferenceUtil::isReferenceTypeName( shape._elementTypeName ) && pElement != nullptr )
                        outListReference.push_back( static_cast<const SceneObjectReference*>( pElement ) );
                }
            }
            else if ( IMapContainerWrapper* pMap = shape._wrapper->asMap() )
            {
                pMap->forEach( pContainer, MapForEachDelegate::create( [&]( const void* pKey, const void* pValue )
                {
                    if ( SceneObjectReferenceUtil::isReferenceTypeName( shape._keyTypeName ) && pKey != nullptr )
                        outListReference.push_back( static_cast<const SceneObjectReference*>( pKey ) );
                    if ( shape._elementNested != nullptr )
                        collectContainerReferences( pValue, *shape._elementNested, outListReference );
                    else if ( SceneObjectReferenceUtil::isReferenceTypeName( shape._elementTypeName ) && pValue != nullptr )
                        outListReference.push_back( static_cast<const SceneObjectReference*>( pValue ) );
                } ) );
            }
        }
    } // namespace

    bool SceneObjectReferenceUtil::findReferenceTo( const SceneManager& sceneManager, const Scene& targetScene, uint64 objectID, string& outHolder )
    {
        outHolder.clear();
        const uint64 fileID = targetScene.findFileID( objectID );
        if ( fileID == 0 || targetScene.getSourcePath().empty() )
            return false;
        vector<const Scene*> listScene;
        sceneManager.collectAllScenes( listScene );
        vector<const SceneObjectReference*> listReference;
        for ( const Scene* scene : listScene )
        {
            if ( scene->getObjectManager() == nullptr )
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
                        if ( outHolder.empty() == false )
                            return;
                        listReference.clear();
                        if ( prop._bIsContainer == SW_TRUE )
                        {
                            if ( prop._containerWrapper != nullptr )
                                collectContainerReferences( prop.getRawPtr( static_cast<const void*>( pComponent ) ), prop.getContainerShape(), listReference );
                        }
                        else if ( isReferenceTypeName( prop._typeName ) )
                        {
                            listReference.push_back( prop.getValuePtr<SceneObjectReference>( pComponent ) );
                        }
                        const auto pointsAtTarget = [&]( const SceneObjectReference* pValue )
                        {
                            return pValue != nullptr && pValue->_fileID == fileID && isSameScenePath( pValue->_scenePath, targetScene.getSourcePath() );
                        };
                        if ( std::ranges::none_of( listReference, pointsAtTarget ) )
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
