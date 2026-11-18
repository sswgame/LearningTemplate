#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/Model/GimmickCircuitEditSession.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphBridge.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuitComponent.h"

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
