#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/Model/GimmickCircuitEditSession.h"

#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/Model/GimmickGraphEditModel.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Circuit/GimmickCircuitComponent.h"

// `GimmickGraphEditModel` 과 `GimmickCircuitComponent` 사이의 읽기 · 쓰기입니다. 컴포넌트의 저장 목록(비공개 PROPERTY)을 리플렉션으로 읽고 씁니다. 컴포넌트는 노드 · 대상 · 배선 목록을 getter 없이 비공개 PROPERTY 로만 둔다(인스펙터와 직렬화기가 리플렉션으로 만진다). 에디터도 같은 길을 쓴다 — 컴포넌트 헤더에 편집기 전용 getter 를 더하지 않는다. 쓴 뒤에는 컴포넌트의 `onPropertyChanged` 를 불러 회로를 다시 짓는다(인스펙터가 속성을 고친 것과 같은 알림).
namespace sw::editor
{
    namespace
    {
        /**
         * @struct GimmickGraphBridge
         * @brief 모델 ↔ 컴포넌트 변환입니다.
         */
        struct GimmickGraphBridge
        {
            /** @brief 컴포넌트가 저장한 목록으로 @p outModel 을 채웁니다. 리플렉션에서 속성을 못 찾으면 false 입니다. */
            [[nodiscard]] static bool load( const GimmickCircuitComponent& component, GimmickGraphEditModel& outModel );
            /** @brief 모델의 목록을 컴포넌트에 쓰고 회로를 다시 짓습니다. 속성을 못 찾으면 false 이고 컴포넌트는 그대로입니다. */
            [[nodiscard]] static bool apply( GimmickCircuitComponent& component, const GimmickGraphEditModel& model );
            /** @brief 컴포넌트가 저장한 내용의 서명입니다(`GimmickGraphEditModel::computeSignature` 와 같은 값). 읽지 못하면 0 입니다. 에디터가 바깥 변경(되돌리기 · 인스펙터)을 알아볼 때 부릅니다. */
            static uint64 computeSignature( const GimmickCircuitComponent& component );
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct GimmickCircuitEditSessionInternal
        {
            /** @brief 겨누는 창의 선택에서 회로를 가진 첫 오브젝트의 회로입니다 — 고른 오브젝트의 컴포넌트를 고치는 창(디테일 패널의 확장)이라 미리보기 창의 오브젝트도 편집한다. */
            [[nodiscard]] static GimmickCircuitComponent* findSelectedCircuit()
            {
                const EditorSelection* pSelection = editor::getFocusedSelection();
                if ( pSelection == nullptr )
                    return nullptr;
                vector<GameObject*> listSelected;
                pSelection->getSelectedObjects( listSelected );
                for ( GameObject* pObject : listSelected )
                {
                    GimmickCircuitComponent* pCircuit = pObject != nullptr ? pObject->getComponent<GimmickCircuitComponent>() : nullptr;
                    if ( pCircuit != nullptr && pCircuit->getOwner() != nullptr )
                        return pCircuit;
                }
                return nullptr;
            }

            [[nodiscard]] static GimmickCircuitComponent* findHeldCircuit( const GameObjectHandle& ownerHandle )
            {
                GameObject* pOwner = ownerHandle.isValid() ? findGameObject( ownerHandle ) : nullptr;
                return pOwner != nullptr ? pOwner->getComponent<GimmickCircuitComponent>() : nullptr;
            }
        };
    } // namespace

    GimmickCircuitSync GimmickCircuitEditSession::sync( GimmickGraphEditModel& outModel, uint64 signature, bool& outLoadFailed )
    {
        outLoadFailed = false;
        // 선택에 회로가 없으면 보던 회로를 이어서 본다 - 대상 오브젝트를 고르려고 다른 오브젝트를 고른 경우다
        GimmickCircuitComponent* pCircuit = GimmickCircuitEditSessionInternal::findSelectedCircuit();
        if ( pCircuit == nullptr )
            pCircuit = GimmickCircuitEditSessionInternal::findHeldCircuit( _ownerHandle );
        if ( pCircuit == nullptr || pCircuit->getOwner() == nullptr )
        {
            _ownerHandle = GameObjectHandle{};
            return GimmickCircuitSync::NoCircuit;
        }

        const GameObjectHandle handle = pCircuit->getOwner()->getHandle();
        GimmickCircuitSync     result = GimmickCircuitSync::Unchanged;
        if ( handle != _ownerHandle )
            result = GimmickCircuitSync::OwnerChanged;
        else if ( GimmickGraphBridge::computeSignature( *pCircuit ) != signature )
            result = GimmickCircuitSync::Reloaded; // 되돌리기나 인스펙터가 바깥에서 바꿨다
        if ( result == GimmickCircuitSync::Unchanged )
            return result;

        _ownerHandle = handle;
        if ( GimmickGraphBridge::load( *pCircuit, outModel ) == false )
        {
            outModel.clear();
            outLoadFailed = true;
        }
        return result;
    }

    string GimmickCircuitEditSession::getOwnerName() const
    {
        string name;
        return findObjectName( _ownerHandle, name ) ? name : string{};
    }

    bool GimmickCircuitEditSession::findObjectName( const GameObjectHandle& handle, string& outName ) const
    {
        const GameObject* pObject = handle.isValid() ? findGameObject( handle ) : nullptr;
        if ( pObject == nullptr )
            return false;
        outName = pObject->getName().c_str();
        return true;
    }

    GameObjectHandle GimmickCircuitEditSession::findFirstSelectedObject() const
    {
        // 회로(`sync`)와 같은 선택에서 고른다 — 대상은 회로와 같은 씬에 있어야 한다.
        const EditorSelection* pSelection = editor::getFocusedSelection();
        vector<GameObject*>    listSelected;
        if ( pSelection != nullptr )
            pSelection->getSelectedObjects( listSelected );
        return listSelected.empty() == false && listSelected[0] != nullptr ? listSelected[0]->getHandle() : GameObjectHandle{};
    }

    bool GimmickCircuitEditSession::commit( const GimmickGraphEditModel& model, const utf8* pLabel )
    {
        GimmickCircuitComponent* pCircuit = GimmickCircuitEditSessionInternal::findHeldCircuit( _ownerHandle );
        if ( pCircuit == nullptr || pCircuit->getOwner() == nullptr )
            return false;
        GameObject* const    pOwner = pCircuit->getOwner();
        const ObjectSnapshot before = EditorTransaction::captureSnapshot( pOwner ); // 컴포넌트는 쓰기 전까지 그대로다
        EditorTransaction    transaction;                                           // 씬을 바꾸는 기록은 거래 구간 안에서만 한다(`EditorTransaction` 의 기록 함수는 인스턴스 멤버다)
        if ( GimmickGraphBridge::apply( *pCircuit, model ) == false )
            return false;
        transaction.recordModify( pOwner, before, EditorTransaction::captureSnapshot( pOwner ), pLabel );
        return true;
    }
} // namespace sw::editor

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
    bool GimmickGraphBridge::load( const GimmickCircuitComponent& component, GimmickGraphEditModel& outModel )
    {
        const PropertyInfo* pNode            = findProperty( component, "_listNode" );
        const PropertyInfo* pTarget          = findProperty( component, "_listNodeTarget" );
        const PropertyInfo* pWire            = findProperty( component, "_listWire" );
        const PropertyInfo* pComment         = findProperty( component, "_listComment" );
        const bool          bMissingProperty = pNode == nullptr || pTarget == nullptr || pWire == nullptr || pComment == nullptr;
        if ( bMissingProperty )
            return false;
        const vector<GimmickNodeDesc>*  pListNode        = pNode->getValuePtr<vector<GimmickNodeDesc>>( static_cast<const void*>( &component ) );
        const vector<GameObjectHandle>* pListTarget      = pTarget->getValuePtr<vector<GameObjectHandle>>( static_cast<const void*>( &component ) );
        const vector<GimmickWireDesc>*  pListWire        = pWire->getValuePtr<vector<GimmickWireDesc>>( static_cast<const void*>( &component ) );
        const vector<GraphCommentBox>*  pListComment     = pComment->getValuePtr<vector<GraphCommentBox>>( static_cast<const void*>( &component ) );
        const bool                      bMissingValuePtr = pListNode == nullptr || pListTarget == nullptr || pListWire == nullptr || pListComment == nullptr;
        if ( bMissingValuePtr )
            return false;
        outModel.populate( *pListNode, *pListTarget, *pListWire, *pListComment );
        return true;
    }

    uint64 GimmickGraphBridge::computeSignature( const GimmickCircuitComponent& component )
    {
        GimmickGraphEditModel scratch( game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry() );
        if ( load( component, scratch ) == false )
            return 0;
        return scratch.computeSignature();
    }

    bool GimmickGraphBridge::apply( GimmickCircuitComponent& component, const GimmickGraphEditModel& model )
    {
        const PropertyInfo* pNode            = findProperty( component, "_listNode" );
        const PropertyInfo* pTarget          = findProperty( component, "_listNodeTarget" );
        const PropertyInfo* pWire            = findProperty( component, "_listWire" );
        const PropertyInfo* pComment         = findProperty( component, "_listComment" );
        const bool          bMissingProperty = pNode == nullptr || pTarget == nullptr || pWire == nullptr || pComment == nullptr;
        if ( bMissingProperty )
            return false;
        vector<GimmickNodeDesc>*  pListNode        = pNode->getValuePtr<vector<GimmickNodeDesc>>( static_cast<void*>( &component ) );
        vector<GameObjectHandle>* pListTarget      = pTarget->getValuePtr<vector<GameObjectHandle>>( static_cast<void*>( &component ) );
        vector<GimmickWireDesc>*  pListWire        = pWire->getValuePtr<vector<GimmickWireDesc>>( static_cast<void*>( &component ) );
        vector<GraphCommentBox>*  pListComment     = pComment->getValuePtr<vector<GraphCommentBox>>( static_cast<void*>( &component ) );
        const bool                bMissingValuePtr = pListNode == nullptr || pListTarget == nullptr || pListWire == nullptr || pListComment == nullptr;
        if ( bMissingValuePtr )
            return false;
        model.exportTo( *pListNode, *pListTarget, *pListWire, *pListComment );
        component.onPropertyChanged( hashed_string( "_listNode" ) ); // 인스펙터가 속성을 고쳤을 때와 같은 알림 - 회로를 다시 짓는다
        return true;
    }
} // namespace sw::editor
