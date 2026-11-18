#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyEditStep.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    namespace
    {
        struct PropertyEditStepInternal
        {
            /** @brief 값 자리 — 컴포넌트(지역 id 가 있으면)나 오브젝트와 그 프로퍼티입니다. 없으면 `_pProp` 가 nullptr 입니다. */
            struct Slot
            {
                Component*          _pComponent{ nullptr };
                void*               _pInstance{ nullptr };
                const PropertyInfo* _pProp{ nullptr };
            };

            static Slot findSlot( GameObject& object, const PropertyEditTarget& target, string_view propertyPath )
            {
                Slot slot;
                if ( target._componentLocalID != 0 )
                {
                    for ( Component* pComponent : object.getComponents() )
                    {
                        if ( pComponent != nullptr && ComponentStableKey::getLocalID( pComponent ) == target._componentLocalID )
                        {
                            slot._pComponent = pComponent;
                            break;
                        }
                    }
                    if ( slot._pComponent == nullptr )
                        return slot;
                    slot._pInstance = slot._pComponent;
                }
                else
                    slot._pInstance = &object;
                const TypeInfo* pType = slot._pComponent != nullptr ? slot._pComponent->getTypeInfo() : object.getTypeInfo();
                if ( pType != nullptr )
                    slot._pProp = pType->findPropertyInHierarchy( hashed_string( string{ propertyPath }.c_str() ) );
                return slot;
            }
        };
    } // namespace

    PropertyEditTarget PropertyEditTarget::make( const GameObject& owner, const Component* pComponent )
    {
        PropertyEditTarget target;
        target._objectID         = owner.getObjectID();
        target._componentLocalID = pComponent != nullptr ? ComponentStableKey::getLocalID( pComponent ) : 0;
        return target;
    }

    void PropertyEditTarget::applyPropertyText( UndoStepContext& context, uint32 sceneKey, string propertyPath, vector<PropertyEditTarget> listTarget, vector<string> listText,
                                                vector<uint32> listTextIndex )
    {
        if ( listTarget.size() != listTextIndex.size() )
        {
            SW_LOG_WARNING_ID( "PropertyEditStep.CountMismatch", "Undo/redo property step has %# targets and %# values - skipped", listTarget.size(), listTextIndex.size() );
            return;
        }
        for ( size_t index = 0; index < listTarget.size(); ++index )
        {
            const uint32 textIndex = listTextIndex[index];
            GameObject*  pObject   = findObject( sceneKey, listTarget[index]._objectID );
            if ( pObject == nullptr || textIndex >= listText.size() || listTarget[index].applyTo( *pObject, propertyPath, listText[textIndex] ) == false )
                continue;
            context._stack.notifyObjectEdit( CommandStack::ObjectEditEvent{ pObject->getObjectID(), ObjectEditKind::Modified } );
        }
    }

    bool PropertyEditTarget::applyTo( GameObject& object, string_view propertyPath, string_view valueText ) const
    {
        const PropertyEditStepInternal::Slot slot = PropertyEditStepInternal::findSlot( object, *this, propertyPath );
        if ( slot._pProp == nullptr )
            return false;
        if ( SerializerUtil::applyPropertyText( *slot._pProp, MutableObjectAddress{ slot._pInstance }, valueText, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
        {
            SW_LOG_WARNING_ID( "PropertyEditStep.CouldNotApply", "Undo/redo could not apply '%#' to %# of '%#'", string{ valueText }.c_str(), string{ propertyPath }.c_str(),
                               object.getName().c_str() );
            return false;
        }
        // 모델의 쓰기 길과 같은 알림(`PropertyGridViewModel::notifyPropertyEdited`) — 빠뜨리면 값에 반응하는 상태(트랜스폼 캐시 · 렌더 상태)가 어긋난다.
        if ( slot._pComponent != nullptr )
            slot._pComponent->onPropertyChanged( slot._pProp->_name );
        else
            object.onPropertyChanged( slot._pProp->_name );
        (void)ObjectValidation::reportGameObject( object, false );
        return true;
    }

    bool PropertyEditTarget::readValueText( GameObject& object, string_view propertyPath, string& outText ) const
    {
        const PropertyEditStepInternal::Slot slot = PropertyEditStepInternal::findSlot( object, *this, propertyPath );
        if ( slot._pProp == nullptr )
            return false;
        outText = SerializerUtil::formatPropertyText( *slot._pProp, ObjectAddress{ slot._pInstance }, SerializeContext::getDefault( SerializePurpose::Authoring ) );
        return true;
    }

    uint32 PropertyEditTarget::findSceneKey( const GameObject& object )
    {
        const SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->getPreviewSceneKeyOf( object.getManager() ) : kInvalidSceneKey;
    }

    GameObject* PropertyEditTarget::findObject( uint32 sceneKey, uint64 objectID )
    {
        const SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return nullptr;
        Scene* pScene = ( sceneKey != kInvalidSceneKey ) ? pSceneManager->findPreviewScene( sceneKey ) : pSceneManager->getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return nullptr;
        GameObject* pObject = pScene->getObjectManager()->findGameObjectByID( objectID );
        return ( pObject != nullptr && pObject->isPendingDestroy() == false ) ? pObject : nullptr;
    }
} // namespace sw::editor
