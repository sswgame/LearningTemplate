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
#include "Engine/Reflection/TypeRegistry.h"
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
        /**
         * @brief 필드 하나의 값 아래에 든 `SceneObjectReference` 를 모읍니다(필드 자체, 구조체 안의 칸, 컨테이너 원소 · 맵 키와 값, 그 중첩). 값을 고치면 포인터가 무효가 됩니다.
         * @details `ReflectedValueWalk` 를 쓰지 않은 이유: 그 걷기는 바이트로 복사할 수 있는 잎을 제자리에서 바꾸는 쓰기 도구이고 `SceneObjectReference` 는 문자열을 가져 잎이 될 수 없다.
         *          여기는 읽기만 하므로 같은 모양(구조체 · 컨테이너 단계)을 읽기 전용으로 내려간다.
         *          구조체 타입은 레지스트리로 찾는다. 레지스트리는 필수 참조라 "없어서 구조체 안을 못 보는" 상태가 만들어지지 않는다.
         */
        class ReferenceCollector
        {
        public:
            ReferenceCollector( const TypeRegistry& registry, vector<const SceneObjectReference*>& outListReference )
                : _registry{ registry }
                , _outListReference{ outListReference }
            {
            }

            void collectProperty( const void* pOwner, const PropertyInfo& prop )
            {
                if ( prop._bIsBitField == SW_TRUE )
                    return;
                const void* pValue = prop.getRawPtr( pOwner );
                if ( prop._bIsContainer == SW_TRUE )
                {
                    if ( prop._containerWrapper != nullptr )
                        collectContainer( pValue, prop.getContainerShape() );
                }
                else
                {
                    collectValue( pValue, prop._typeName );
                }
            }

        private:
            void collectValue( const void* pValue, const hashed_string& typeName )
            {
                if ( pValue == nullptr || typeName.empty() )
                    return;
                if ( SceneObjectReferenceUtil::isReferenceTypeName( typeName ) )
                {
                    _outListReference.push_back( static_cast<const SceneObjectReference*>( pValue ) );
                    return;
                }
                // 구조체 값의 주소는 그 구조체 타입의 객체 주소다. 내장 값 타입(숫자 · 문자열 등)은 구조체가 아니다.
                const TypeInfo* pStruct = _registry.findType( typeName );
                if ( pStruct == nullptr || pStruct->isPrimitive() )
                    return;
                for ( const PropertyInfo& inner : pStruct->getPropertiesWithBase() )
                {
                    collectProperty( pValue, inner );
                }
            }

            void collectContainer( const void* pContainer, const NestedContainerInfo& shape )
            {
                if ( pContainer == nullptr || shape._wrapper == nullptr )
                    return;
                if ( ISequenceContainerWrapper* pSequence = shape._wrapper->asSequence() )
                {
                    const size_t elementCount = pSequence->getSize( pContainer );
                    for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
                    {
                        collectElement( pSequence->getElementConst( pContainer, elementIndex ), shape );
                    }
                }
                else if ( IMapContainerWrapper* pMap = shape._wrapper->asMap() )
                {
                    pMap->forEach( pContainer, MapForEachDelegate::create( [&]( const void* pKey, const void* pValue )
                    {
                        collectValue( pKey, shape._keyTypeName );
                        collectElement( pValue, shape );
                    } ) );
                }
            }

            void collectElement( const void* pElement, const NestedContainerInfo& shape )
            {
                if ( shape._elementNested != nullptr )
                    collectContainer( pElement, *shape._elementNested );
                else
                    collectValue( pElement, shape._elementTypeName );
            }

            const TypeRegistry&                  _registry;
            vector<const SceneObjectReference*>& _outListReference;
        };
    } // namespace

    bool SceneObjectReferenceUtil::findReferenceTo( const SceneManager& sceneManager, const TypeRegistry& typeRegistry, const Scene& targetScene, uint64 objectID, string& outHolder )
    {
        outHolder.clear();
        const uint64 fileID = targetScene.findFileID( objectID );
        if ( fileID == 0 || targetScene.getSourcePath().empty() )
            return false;
        vector<const Scene*> listScene;
        sceneManager.collectAllScenes( listScene );
        vector<const SceneObjectReference*> listReference;
        ReferenceCollector                  collector( typeRegistry, listReference );
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
                        collector.collectProperty( static_cast<const void*>( pComponent ), prop );
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
