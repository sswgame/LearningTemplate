#include "pch.h"

#include "Editor/Common/Workspace/EditorSelectionSync.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/2D/SpriteComponent.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSelectionSyncInternal
        {
            /** @brief 문맥 @p context 칸의 번호입니다(감시 값 — 시나리오가 "이 문맥이 바뀌었나" 를 Remember · sameAs 로 본다). 채널이 없으면 값을 내지 않습니다. */
            [[nodiscard]] static bool readSequence( EditorSelectionContext context, float64& outValue )
            {
                const EditorSelectionSync* pSync = editor::findSubsystem<EditorSelectionSync>();
                if ( pSync == nullptr )
                    return false;
                outValue = static_cast<float64>( pSync->getEntry( context )._sequence );
                return true;
            }

            [[nodiscard]] static bool readMainSequence( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readSequence( EditorSelectionContext::MainScene, outValue );
            }

            [[nodiscard]] static bool readPreviewSequence( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readSequence( EditorSelectionContext::PreviewScene, outValue );
            }

            /** @brief 메인 씬 칸에 애셋이 있으면 1 입니다(콘텐츠 브라우저가 테두리로 보일 애셋). */
            [[nodiscard]] static bool readMainHasAsset( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorSelectionSync* pSync = editor::findSubsystem<EditorSelectionSync>();
                if ( pSync == nullptr )
                    return false;
                outValue = pSync->getEntry( EditorSelectionContext::MainScene )._assetPath.empty() ? 0.0 : 1.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorSelectionSyncMainSequence, "Editor.SelectionSyncMainSequence", "Channel number of the last main-scene selection change (the selection sync channel)",
                         &EditorSelectionSyncInternal::readMainSequence );
    SW_AUTOMATION_WATCH( editorSelectionSyncPreviewSequence, "Editor.SelectionSyncPreviewSequence", "Channel number of the last preview-window selection change (the selection sync channel)",
                         &EditorSelectionSyncInternal::readPreviewSequence );
    SW_AUTOMATION_WATCH( editorSelectionSyncMainHasAsset, "Editor.SelectionSyncMainHasAsset", "1 when the object selected in the main scene comes from an asset (prefab, mesh, sprite texture)",
                         &EditorSelectionSyncInternal::readMainHasAsset );

    void EditorSelectionSync::onSelectionChanged( const EditorSelection& selection )
    {
        const GameObject* pPrimary = selection.getPrimaryObject();
        const string      asset    = pPrimary != nullptr ? findObjectAssetPath( *pPrimary ) : string{};
        const uint64      objectID = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
        if ( selection.isPreviewScope() )
        {
            // 미리보기 창의 선택은 자기 칸에만 — 메인 씬 칸 · 애셋 칸을 건드리지 않는다(창의 부품이 메인 씬을 덮지 않는다).
            publish( EditorSelectionContext::PreviewScene, asset, objectID );
            return;
        }
        publish( EditorSelectionContext::MainScene, asset, objectID );
    }

    void EditorSelectionSync::publish( EditorSelectionContext context, string_view assetPath, uint64 objectID )
    {
        EditorSelectionSyncEntry& entry = _arrEntry[static_cast<size_t>( context )];
        if ( entry._sequence != 0 && string_view{ entry._assetPath } == assetPath && entry._objectID == objectID )
            return;
        entry._assetPath = string{ assetPath };
        entry._objectID  = objectID;
        entry._context   = context;
        entry._sequence  = _nextSequence++;
    }

    const EditorSelectionSyncEntry& EditorSelectionSync::getEntry( EditorSelectionContext context ) const
    {
        return _arrEntry[static_cast<size_t>( context )];
    }

    const EditorSelectionSyncEntry* EditorSelectionSync::findLatest( uint8 acceptedMask ) const
    {
        const EditorSelectionSyncEntry* pLatest = nullptr;
        for ( const EditorSelectionSyncEntry& entry : _arrEntry )
        {
            const bool bAccepted = ( acceptedMask & makeSelectionContextBit( entry._context ) ) != 0 && entry._sequence != 0 && entry._assetPath.empty() == false;
            if ( bAccepted && ( pLatest == nullptr || entry._sequence > pLatest->_sequence ) )
                pLatest = &entry;
        }
        return pLatest;
    }

    string EditorSelectionSync::findObjectAssetPath( const GameObject& object )
    {
        // 프리팹 인스턴스가 먼저다 — 프리팹 안의 메시가 아니라 놓은 프리팹이 그 오브젝트의 애셋이다(언리얼 Sync to Content Browser 가 액터의 블루프린트를 고르는 것과 같다).
        const EditorWorkspace* pWorkspace = editor::findSubsystem<EditorWorkspace>();
        if ( pWorkspace != nullptr )
        {
            const string_view prefabPath = pWorkspace->findPrefabPath( &object );
            if ( prefabPath.empty() == false )
                return string{ prefabPath };
        }
        const MeshComponent* pMesh = object.getComponent<MeshComponent>();
        if ( pMesh != nullptr && pMesh->getMeshID().empty() == false )
            return pMesh->getMeshID();
        const SpriteComponent* pSprite = object.getComponent<SpriteComponent>();
        if ( pSprite != nullptr && pSprite->getTextureName().empty() == false )
            return pSprite->getTextureName();
        return string{};
    }
} // namespace sw::editor
