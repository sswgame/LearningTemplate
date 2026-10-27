#include "pch.h"

#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/ReflectionValidation.h"

SW_LOG_CALLER( "ObjectValidation" );
namespace sw
{
    uint32 ObjectValidation::validateGameObject( const GameObject& object, ValidationContext& context )
    {
        context.setSource( object.getObjectID(), object.getName().view() );
        uint32 issueCount = 0;
        for ( const Component* pComp : object.getComponents() )
        {
            const TypeInfo* pType = ( pComp != nullptr && pComp->isPendingDestroy() == false ) ? pComp->getTypeInfo() : nullptr;
            if ( pType == nullptr || ReflectionValidation::hasValidator( *pType ) == false )
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

    SW_SCENE_SERVICE( SceneValidationIssues, DestroyPost )

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
