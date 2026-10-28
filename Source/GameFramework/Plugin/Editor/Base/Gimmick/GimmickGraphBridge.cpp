#include "pch.h"

#include "GameFramework/Plugin/Editor/Base/Gimmick/GimmickGraphBridge.h"

#include "Engine/Reflection/ReflectionTypes.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 컴포넌트 타입에서 PROPERTY 하나를 찾습니다. 상속분도 봅니다. */
        const PropertyInfo* findProperty( const GimmickCircuitComponent& component, const utf8* pName )
        {
            const TypeInfo* pType = component.getTypeInfo();
            return pType != nullptr ? pType->findPropertyInHierarchy( hashed_string( pName ) ) : nullptr;
        }
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool GimmickGraphBridge::load( const GimmickCircuitComponent& component, GimmickGraphModel& outModel )
    {
        const PropertyInfo* pNode            = findProperty( component, "_listNode" );
        const PropertyInfo* pTarget          = findProperty( component, "_listNodeTarget" );
        const PropertyInfo* pWire            = findProperty( component, "_listWire" );
        const bool          bMissingProperty = pNode == nullptr || pTarget == nullptr || pWire == nullptr;
        if ( bMissingProperty )
            return false;
        const vector<GimmickNodeDesc>*  pListNode        = pNode->getValuePtr<vector<GimmickNodeDesc>>( static_cast<const void*>( &component ) );
        const vector<GameObjectHandle>* pListTarget      = pTarget->getValuePtr<vector<GameObjectHandle>>( static_cast<const void*>( &component ) );
        const vector<GimmickWireDesc>*  pListWire        = pWire->getValuePtr<vector<GimmickWireDesc>>( static_cast<const void*>( &component ) );
        const bool                      bMissingValuePtr = pListNode == nullptr || pListTarget == nullptr || pListWire == nullptr;
        if ( bMissingValuePtr )
            return false;
        outModel.populate( *pListNode, *pListTarget, *pListWire );
        return true;
    }

    uint64 GimmickGraphBridge::computeSignature( const GimmickCircuitComponent& component )
    {
        GimmickGraphModel scratch( GimmickNodeRegistry::getDefault() );
        if ( load( component, scratch ) == false )
            return 0;
        return scratch.computeSignature();
    }

    bool GimmickGraphBridge::apply( GimmickCircuitComponent& component, const GimmickGraphModel& model )
    {
        const PropertyInfo* pNode            = findProperty( component, "_listNode" );
        const PropertyInfo* pTarget          = findProperty( component, "_listNodeTarget" );
        const PropertyInfo* pWire            = findProperty( component, "_listWire" );
        const bool          bMissingProperty = pNode == nullptr || pTarget == nullptr || pWire == nullptr;
        if ( bMissingProperty )
            return false;
        vector<GimmickNodeDesc>*  pListNode        = pNode->getValuePtr<vector<GimmickNodeDesc>>( static_cast<void*>( &component ) );
        vector<GameObjectHandle>* pListTarget      = pTarget->getValuePtr<vector<GameObjectHandle>>( static_cast<void*>( &component ) );
        vector<GimmickWireDesc>*  pListWire        = pWire->getValuePtr<vector<GimmickWireDesc>>( static_cast<void*>( &component ) );
        const bool                bMissingValuePtr = pListNode == nullptr || pListTarget == nullptr || pListWire == nullptr;
        if ( bMissingValuePtr )
            return false;
        model.exportTo( *pListNode, *pListTarget, *pListWire );
        component.onPropertyChanged( hashed_string( "_listNode" ) ); // 인스펙터가 속성을 고쳤을 때와 같은 알림 - 회로를 다시 짓는다
        return true;
    }
} // namespace sw::editor
