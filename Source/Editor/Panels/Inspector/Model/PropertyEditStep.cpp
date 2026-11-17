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
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    void writeUndoArg( Archive& archive, const PropertyEditTarget& target )
    {
        archive << target._objectID;
        archive.writeString( target._componentKey );
        archive.writeString( target._propertyPath );
    }

    bool readUndoArg( Archive& archive, PropertyEditTarget& outTarget )
    {
        archive >> outTarget._objectID;
        return archive.readString( outTarget._componentKey ) && archive.readString( outTarget._propertyPath ) && archive.isOk();
    }

    namespace
    {
        struct PropertyEditStepInternal
        {
            /** @brief 값 자리 — 컴포넌트(키가 있으면)나 오브젝트와 그 프로퍼티입니다. 없으면 `_pProp` 가 nullptr 입니다. */
            struct Slot
            {
                Component*          _pComponent{ nullptr };
                void*               _pInstance{ nullptr };
                const PropertyInfo* _pProp{ nullptr };
            };

            static Slot findSlot( GameObject& object, const PropertyEditTarget& target )
            {
                Slot slot;
                if ( target._componentKey.empty() == false )
                {
                    slot._pComponent = ComponentStableKey::findComponent( &object, target._componentKey );
                    if ( slot._pComponent == nullptr )
                        return slot;
                    slot._pInstance = slot._pComponent;
                }
                else
                    slot._pInstance = &object;
                const TypeInfo* pType = slot._pComponent != nullptr ? slot._pComponent->getTypeInfo() : object.getTypeInfo();
                if ( pType != nullptr )
                    slot._pProp = pType->findPropertyInHierarchy( hashed_string( target._propertyPath.c_str() ) );
                return slot;
            }
        };
    } // namespace

    void PropertyEditTarget::applyPropertyText( UndoStepContext& context, uint32 sceneKey, vector<PropertyEditTarget> listTarget, vector<string> listValueText )
    {
        if ( listTarget.size() != listValueText.size() )
        {
            SW_LOG_WARNING_ID( "PropertyEditStep.CountMismatch", "Undo/redo property step has %# targets and %# values - skipped", listTarget.size(), listValueText.size() );
            return;
        }
        for ( size_t index = 0; index < listTarget.size(); ++index )
        {
            GameObject* pObject = findObject( sceneKey, listTarget[index]._objectID );
            if ( pObject == nullptr || listTarget[index].applyTo( *pObject, listValueText[index] ) == false )
                continue;
            context._stack.notifyObjectEdit( CommandStack::ObjectEditEvent{ pObject->getObjectID(), ObjectEditKind::Modified } );
        }
    }

    bool PropertyEditTarget::applyTo( GameObject& object, string_view valueText ) const
    {
        const PropertyEditStepInternal::Slot slot = PropertyEditStepInternal::findSlot( object, *this );
        if ( slot._pProp == nullptr )
            return false;
        if ( SerializerUtil::applyPropertyText( *slot._pProp, MutableObjectAddress{ slot._pInstance }, valueText, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
        {
            SW_LOG_WARNING_ID( "PropertyEditStep.CouldNotApply", "Undo/redo could not apply '%#' to %# of '%#'", string{ valueText }.c_str(), _propertyPath.c_str(), object.getName().c_str() );
            return false;
        }
        // 모델의 쓰기 길과 같은 알림(`PropertyGridModel::notifyPropertyEdited`) — 빠뜨리면 값에 반응하는 상태(트랜스폼 캐시 · 렌더 상태)가 어긋난다.
        if ( slot._pComponent != nullptr )
            slot._pComponent->onPropertyChanged( slot._pProp->_name );
        else
            object.onPropertyChanged( slot._pProp->_name );
        (void)ObjectValidation::reportGameObject( object, false );
        return true;
    }

    bool PropertyEditTarget::readValueText( GameObject& object, string& outText ) const
    {
        const PropertyEditStepInternal::Slot slot = PropertyEditStepInternal::findSlot( object, *this );
        if ( slot._pProp == nullptr )
            return false;
        outText = SerializerUtil::formatPropertyText( *slot._pProp, ObjectAddress{ slot._pInstance }, SerializeContext::getDefault( SerializePurpose::Authoring ) );
        return true;
    }

    PropertyEditTarget PropertyEditTarget::make( const GameObject& owner, const Component* pComponent, const PropertyInfo& prop )
    {
        PropertyEditTarget target;
        target._objectID     = owner.getObjectID();
        target._componentKey = pComponent != nullptr ? ComponentStableKey::makeKey( pComponent ) : string{};
        target._propertyPath = prop._name.c_str();
        return target;
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
