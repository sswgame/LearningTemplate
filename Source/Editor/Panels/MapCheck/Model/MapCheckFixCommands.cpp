#include "pch.h"

#include "Editor/Panels/MapCheck/Model/MapCheckFixCommands.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    namespace
    {
        struct MapCheckFixCommandsInternal
        {
            /** @brief 오브젝트에서 타입 이름이 @p typeName 인 첫 컴포넌트입니다. 없으면 nullptr 입니다. */
            static Component* findComponent( const GameObject& object, const hashed_string& typeName )
            {
                Component* pFound = nullptr;
                object.forEachComponent( [&pFound, &typeName]( Component* pComponent )
                {
                    const TypeInfo* pType = pComponent->findCachedTypeInfo();
                    if ( pFound == nullptr && pType != nullptr && pType->_fullyQualifiedName == typeName )
                        pFound = pComponent;
                } );
                return pFound;
            }

            /** @brief 고침 하나를 씁니다. 쓰면 true 이고, 찾지 못했거나 쓰지 못했으면 이유를 경고하고 false 입니다. */
            static bool applyOne( GameObject& object, const ValidationIssue& issue )
            {
                Component* pComponent = findComponent( object, issue._typeName );
                if ( pComponent == nullptr )
                {
                    SW_LOG_WARNING_ID( "MapCheckFixCommands.ComponentGone", "Fix skipped - '%#' has no %# any more", object.getName().c_str(), issue._typeName.c_str() );
                    return false;
                }
                const TypeInfo*     pType = pComponent->findCachedTypeInfo();
                const PropertyInfo* pProp = pType != nullptr ? pType->findPropertyInHierarchy( issue._fix._propertyName ) : nullptr;
                if ( pProp == nullptr )
                {
                    SW_LOG_WARNING_ID( "MapCheckFixCommands.PropertyGone", "Fix skipped - %# has no property '%#'", issue._typeName.c_str(), issue._fix._propertyName.c_str() );
                    return false;
                }
                if ( SerializerUtil::applyPropertyText( *pProp, MutableObjectAddress{ pComponent }, issue._fix._valueText, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
                {
                    SW_LOG_WARNING_ID( "MapCheckFixCommands.ValueRejected", "Fix skipped - '%#' could not be written to %#.%#", issue._fix._valueText, issue._typeName.c_str(), pProp->_name.c_str() );
                    return false;
                }
                pComponent->onPropertyChanged( pProp->_name );
                return true;
            }
        };
    } // namespace

    bool MapCheckFixCommands::canFix( const ValidationIssue& issue )
    {
        return issue._fix.isValid() && issue._sourceID != 0;
    }

    uint32 MapCheckFixCommands::applyFixes( GameObjectManager* pManager, const vector<ValidationIssue>& listIssue, const utf8* pUndoLabel )
    {
        EditorTransaction transaction; // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        if ( pManager == nullptr || EditorPlaySession::refuseSceneEdit( "Map Check fix" ) )
            return 0;
        uint32 fixedCount = 0;
        EditorTransaction::beginTransaction( pUndoLabel );
        for ( const ValidationIssue& issue : listIssue )
        {
            if ( canFix( issue ) == false )
                continue;
            GameObject* pObject = pManager->findGameObjectByID( issue._sourceID );
            if ( pObject == nullptr )
                continue; // 지운 오브젝트의 결과다 — 표가 곧 비운다
            const ObjectSnapshot before = EditorTransaction::captureSnapshot( pObject );
            if ( MapCheckFixCommandsInternal::applyOne( *pObject, issue ) == false )
                continue;
            EditorSceneCommands::commitModify( transaction, pObject, before, pUndoLabel );
            (void)ObjectValidation::reportGameObject( *pObject, false ); // 고친 결과가 표에서 빠진다(같은 출처를 다시 검증)
            ++fixedCount;
        }
        if ( fixedCount == 0 )
            EditorTransaction::cancelTransaction();
        else
            EditorTransaction::endTransaction();
        SW_LOG_INFO_ID( "MapCheckFixCommands.Applied", "Map Check: applied %# fix(es)", fixedCount );
        return fixedCount;
    }
} // namespace sw::editor
