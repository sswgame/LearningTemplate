#include "pch.h"

#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw
{
    namespace
    {
        struct ObjectValidationInternal
        {
            /**
             * @brief 빈 필수 이름 칸(`PROPERTY( Required )` 의 이름 · 경로 형식)을 경고로 남깁니다. 남긴 수를 돌려줍니다.
             * @details 에디터 저장은 빈 칸도 속성으로 적어 "적힌" 것이 되지만, 다음 로드의 필수 칸 검사(`SerializerUtil::isRequiredValueMissing`)는 빈 이름을 빠진 것으로 본다.
             *          저장 · 인스펙터 · 맵 점검이 같은 검증을 지나므로 여기서 미리 알린다. 판정은 로드와 같은 형식 목록(`isNameValueType`)이다.
             */
            static uint32 validateRequiredNames( const TypeInfo& typeInfo, const Component* pComp, ValidationContext& context )
            {
                uint32                  issueCount = 0;
                const SerializeContext& serialize  = SerializeContext::getDefault( SerializePurpose::Authoring );
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( prop._metadata->_bRequired == SW_FALSE || prop._bIsContainer == SW_TRUE || SerializerUtil::isNameValueType( prop._typeName ) == false )
                        continue;
                    StringBuilder<constant::buffer::kMax8192> text;
                    SerializerUtil::valueToText( text, ValueAddress{ prop.getRawPtr( static_cast<const void*>( pComp ) ) }, prop._typeName, serialize );
                    const string_view written = text.view();
                    if ( SerializerUtil::isRequiredValueMissing( prop, true, &written ) == false )
                        continue;
                    context.setScope( typeInfo._name, prop._name );
                    context.addWarning( "required name is empty - write it in the prefab or scene (the next load fails: ObjectStateSerializer.RequiredMissing)" );
                    ++issueCount;
                }
                return issueCount;
            }
        };
    } // namespace

    uint32 ObjectValidation::validateGameObject( const GameObject& object, ValidationContext& context )
    {
        context.setSource( object.getObjectID(), object.getName().view() );
        uint32 issueCount = 0;
        for ( const Component* pComp : object.getComponents() )
        {
            const TypeInfo* pType = ( pComp != nullptr && pComp->isPendingDestroy() == false ) ? pComp->getTypeInfo() : nullptr;
            if ( pType == nullptr )
                continue;
            issueCount += ObjectValidationInternal::validateRequiredNames( *pType, pComp, context );
            if ( ReflectionValidation::hasValidator( *pType ) == false )
                continue;
            issueCount += ReflectionValidation::validateObject( *pType, pComp, context );
        }
        return issueCount;
    }

    uint32 ObjectValidation::reportGameObject( const GameObject& object, const bool bLog )
    {
        ValidationContext        context;
        const uint32             issueCount = validateGameObject( object, context );
        GameObjectManager* const pManager   = object.getManager();
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

    SW_SCENE_SERVICE( SceneValidationIssues, DestroyPost, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure } ) )

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
