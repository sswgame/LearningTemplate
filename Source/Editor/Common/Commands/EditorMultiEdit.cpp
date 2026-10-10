#include "pch.h"

#include "Editor/Common/Commands/EditorMultiEdit.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw::editor
{
    void EditorMultiEditUtil::collectCommonComponents( const vector<GameObject*>& listObject, vector<EditorMultiEditComponent>& outListCommon )
    {
        outListCommon.clear();
        if ( listObject.empty() || listObject[0] == nullptr )
            return;
        for ( Component* pPrimary : listObject[0]->getComponents() )
        {
            if ( pPrimary == nullptr || pPrimary->getTypeInfo() == nullptr )
                continue;
            const TypeInfo* pType          = pPrimary->getTypeInfo();
            bool            bDuplicateType = false;
            for ( const EditorMultiEditComponent& common : outListCommon )
            {
                bDuplicateType = bDuplicateType || common._pType == pType;
            }
            if ( bDuplicateType )
                continue; // 같은 타입이 둘이면 첫 것만 — 짝을 고를 근거가 없다
            EditorMultiEditComponent common{};
            common._pType = pType;
            for ( GameObject* pObject : listObject )
            {
                Component* pMatch = nullptr;
                if ( pObject != nullptr )
                {
                    for ( Component* pComponent : pObject->getComponents() )
                    {
                        if ( pMatch == nullptr && pComponent != nullptr && pComponent->getTypeInfo() == pType )
                            pMatch = pComponent;
                    }
                }
                if ( pMatch == nullptr )
                    break;
                common._listComponent.push_back( pMatch );
            }
            if ( common._listComponent.size() == listObject.size() )
                outListCommon.push_back( std::move( common ) );
        }
    }

    bool EditorMultiEditUtil::hasMixedValues( const PropertyInfo& prop, const vector<const void*>& listInstance )
    {
        if ( listInstance.size() < 2 )
            return false;
        const SerializeContext& context = SerializeContext::getDefault();
        const string            first   = SerializerUtil::formatPropertyText( prop, listInstance[0], context );
        for ( size_t index = 1; index < listInstance.size(); ++index )
        {
            if ( SerializerUtil::formatPropertyText( prop, listInstance[index], context ) != first )
                return true;
        }
        return false;
    }

    uint32 EditorMultiEditUtil::copyPropertyToOthers( const PropertyInfo& prop, const vector<Component*>& listComponent )
    {
        if ( listComponent.size() < 2 || listComponent[0] == nullptr )
            return 0;
        const SerializeContext& context = SerializeContext::getDefault();
        const string            text    = SerializerUtil::formatPropertyText( prop, listComponent[0], context );
        uint32                  copiedCount{ 0 };
        for ( size_t index = 1; index < listComponent.size(); ++index )
        {
            Component* pComponent = listComponent[index];
            if ( pComponent == nullptr || SerializerUtil::applyPropertyText( prop, pComponent, text, context ) == false )
                continue;
            pComponent->onPropertyChanged( prop._name );
            ++copiedCount;
        }
        return copiedCount;
    }
} // namespace sw::editor
