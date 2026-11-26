/**
 * @file ObjectValidation.cpp
 * @brief 오브젝트 검증(`ObjectValidation`)의 구현과 월드의 결과 표(`SceneValidationIssues`)입니다. `Dev/` 라 배포 구성은 짓지 않는다(입구는 헤더의 인라인 함수).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/Dev/SceneValidationIssues.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/PropertyConstraints.h"
#include "Engine/Serialization/Serializer/ValueSerializer.h"

namespace sw
{
    namespace
    {
        struct ObjectValidationInternal
        {
            /**
             * @brief 빈 필수 이름 칸(`PROPERTY( Required )` 의 이름 · 경로 형식)마다 @p onEmpty 를 부르고 그 수를 돌려줍니다.
             * @details 에디터 저장은 빈 칸도 속성으로 적어 "적힌" 것이 되지만, 다음 로드의 필수 칸 검사(`PropertyConstraints::isRequiredValueMissing`)는 빈 이름을 빠진 것으로 본다.
             *          저장 · 인스펙터 · 맵 점검이 같은 검증을 지나므로 여기서 미리 알린다. 판정은 로드와 같은 형식 목록(`isNameValueType`)이다.
             */
            template <typename TOnEmpty>
            static uint32 forEachEmptyRequiredName( const TypeInfo& typeInfo, const Component* pComp, TOnEmpty onEmpty )
            {
                if ( typeInfo.hasDerivedFlags() == false || typeInfo.hasRequiredProperty() == false )
                    return 0; // 레지스트리 밖 사본(테스트 목)은 필수 칸 판정이 없다
                uint32                  issueCount = 0;
                const SerializeContext& serialize  = SerializeContext::getDefault( SerializePurpose::Authoring );
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( prop._metadata->_bRequired == SW_FALSE || prop._bIsContainer == SW_TRUE || PropertyConstraints::isNameValueType( prop._typeName ) == false )
                        continue;
                    StringBuilder<constant::buffer::kMax8192> text;
                    ValueSerializer::valueToText( text, ValueAddress{ prop.getRawPtr( static_cast<const void*>( pComp ) ) }, prop._typeName, serialize );
                    const string_view written = text.view();
                    if ( PropertyConstraints::isRequiredValueMissing( prop, true, &written, RequiredScope::Root ) == false )
                        continue;
                    onEmpty( prop );
                    ++issueCount;
                }
                return issueCount;
            }
        };
    } // namespace

    uint32 ObjectValidation::validateGameObjectInternal( const GameObject& object, ValidationContext& context )
    {
        context.setSource( object.getObjectID(), object.getName().view() );
        uint32 issueCount = 0;
        for ( const Component* pComp : object.getComponents() )
        {
            const TypeInfo* pType = ( pComp != nullptr && pComp->isPendingDestroy() == false ) ? pComp->getTypeInfo() : nullptr;
            if ( pType == nullptr )
                continue;
            issueCount += ObjectValidationInternal::forEachEmptyRequiredName( *pType, pComp, [&context, pType]( const PropertyInfo& prop )
            {
                context.setScope( pType->_name, prop._name );
                context.addWarning( "required name is empty - write it in the prefab or scene (the next load fails: ObjectStateSerializer.RequiredMissing)" );
            } );
            if ( ReflectionValidation::hasValidator( *pType ) == false )
                continue;
            issueCount += ReflectionValidation::validateObject( *pType, pComp, context );
        }
        return issueCount;
    }

    uint32 ObjectValidation::reportGameObjectInternal( const GameObject& object, const bool bLog )
    {
        ValidationContext        context;
        const uint32             issueCount = validateGameObjectInternal( object, context );
        GameObjectManager* const pManager   = object.getMutableManager();
        // 결과는 오브젝트가 사는 월드의 표다. 매니저 밖에서 만든 오브젝트는 둘 월드가 없어 결과 수만 돌려준다(로그는 아래가 남긴다).
        if ( pManager != nullptr )
            pManager->getSubsystem<SceneValidationIssues>().getLog().replaceIssues( object.getObjectID(), context.getIssues() );
        if ( bLog )
        {
            for ( const ValidationIssue& issue : context.getIssues() )
            {
                // id 가 있어 시나리오가 `<ExpectLog id="ObjectValidation.Issue">` 로 선언한다 — 문구는 오브젝트 · 속성마다 조립돼 본문 전체 일치로는 선언할 수 없다.
                SW_LOG_WARNING_ID( "ObjectValidation.Issue", "Validation %# on '%#' (%#%#%#): %#", issue._severity == ValidationSeverity::Error ? "error" : "warning", issue._sourceLabel.c_str(),
                                   issue._typeName.c_str(), issue._propertyName.empty() ? "" : "::", issue._propertyName.c_str(), issue._message.c_str() );
            }
        }
        return issueCount;
    }

    uint32 ObjectValidation::reportEmptyRequiredInternal( const Component& component )
    {
        const TypeInfo* pType = component.getTypeInfo();
        if ( pType == nullptr )
            return 0;
        return ObjectValidationInternal::forEachEmptyRequiredName( *pType, &component, [&component, pType]( const PropertyInfo& prop )
        {
            const GameObject* pOwner = component.getOwner();
            SW_LOG_ERROR_ID( "ObjectValidation.RequiredEmptyAtBeginPlay", "%# on '%#' begins play with its required property %# empty - fill it where the component is added", pType->_name.c_str(),
                             pOwner != nullptr ? pOwner->getName().c_str() : "", prop._name.c_str() );
        } );
    }

    void ObjectValidation::removeGameObjectIssuesInternal( GameObjectManager* const pManager, const uint64 objectID )
    {
        // 표는 자기 월드의 것이다 — 매니저가 소멸하는 중이어도 서비스는 오브젝트보다 늦게 내려가므로(저장소를 먼저 비운다) 아직 있다.
        SceneValidationIssues* const pIssues = pManager != nullptr ? pManager->getSceneScope().find<SceneValidationIssues>() : nullptr;
        if ( pIssues != nullptr )
            pIssues->getLog().removeSource( objectID );
    }

    SW_SCENE_SUBSYSTEM( SceneValidationIssues, DestroyPost, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ) )

    SceneValidationIssues::SceneValidationIssues( GameObjectManager& /*manager*/ )
        : _log{}
    {
    }

    void SceneValidationIssues::update( float32 /*deltaTime*/ )
    {
    }

    void SceneValidationIssues::onSceneCleared()
    {
        _log.clear();
    }
} // namespace sw
