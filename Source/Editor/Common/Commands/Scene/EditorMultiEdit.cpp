#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorMultiEdit.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

namespace sw::editor
{
    void EditorMultiEditGroup::collectCommonComponents( const vector<GameObject*>& listObject, vector<EditorMultiEditGroup>& outListCommon )
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
            for ( const EditorMultiEditGroup& common : outListCommon )
            {
                bDuplicateType = bDuplicateType || common._pType == pType;
            }
            if ( bDuplicateType )
                continue; // 같은 타입이 둘이면 첫 것만 — 짝을 고를 근거가 없다
            EditorMultiEditGroup common{};
            common._pType = pType;
            for ( GameObject* pObject : listObject )
            {
                Component* pMatch = nullptr;
                if ( pObject != nullptr )
                {
                    for ( Component* pComponent : pObject->getComponents() )
                    {
                        const bool bFirstMatchingComponent = pMatch == nullptr && pComponent != nullptr && pComponent->getTypeInfo() == pType;
                        if ( bFirstMatchingComponent )
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

    bool EditorMultiEditGroup::hasMixedValues( const PropertyInfo& prop, const vector<ObjectAddress>& listInstance )
    {
        const SerializeContext& context     = SerializeContext::getDefault( SerializePurpose::Authoring );
        const bool              bComparable = listInstance.size() >= 2 && SerializerUtil::canCarryProperty( prop, context );
        if ( bComparable == false )
            return false;
        for ( size_t index = 1; index < listInstance.size(); ++index )
        {
            if ( SerializerUtil::arePropertyValuesEqual( prop, listInstance[0], listInstance[index], context ) == false )
                return true;
        }
        return false;
    }
} // namespace sw::editor
