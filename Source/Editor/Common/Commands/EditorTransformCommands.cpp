#include "pch.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTransformCommandsInternal
        {
            static float32 worldAxisValue( const float3& pos, AlignAxis axis )
            {
                if ( axis == AlignAxis::X )
                    return pos._x;
                if ( axis == AlignAxis::Y )
                    return pos._y;
                return pos._z;
            }

            static void setLocalAxisValue( float3& pos, AlignAxis axis, float32 value )
            {
                if ( axis == AlignAxis::X )
                    pos._x = value;
                else if ( axis == AlignAxis::Y )
                    pos._y = value;
                else
                    pos._z = value;
            }

            struct AlignSortLess
            {
                AlignAxis _axis{ AlignAxis::X };

                bool operator()( const GameObject* pLeft, const GameObject* pRight ) const
                {
                    if ( pLeft == nullptr || pRight == nullptr )
                        return false;
                    const SceneComponent* pLeftSc  = pLeft->getPrimarySceneComponent();
                    const SceneComponent* pRightSc = pRight->getPrimarySceneComponent();
                    const float3          posA     = pLeftSc != nullptr ? pLeftSc->getWorldPosition() : float3{};
                    const float3          posB     = pRightSc != nullptr ? pRightSc->getWorldPosition() : float3{};
                    return worldAxisValue( posA, _axis ) < worldAxisValue( posB, _axis );
                }
            };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorTransformCommands::pasteComponentValues( Component* pTargetComp, const vector<uint8>& bytes, string_view xmlFallback )
    {
        if ( pTargetComp == nullptr || pTargetComp->getTypeInfo() == nullptr )
            return false;

        GameObject* const          pOwner         = pTargetComp->getOwner();
        const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pOwner );

        bool bSuccess = false;
        if ( bytes.empty() == false )
            bSuccess = BinarySerializer::deserialize( pTargetComp, *pTargetComp->getTypeInfo(), bytes.data(), bytes.size() );

        if ( bSuccess == false && xmlFallback.empty() == false )
            bSuccess = XmlSerializer::deserialize( pTargetComp, *pTargetComp->getTypeInfo(), string{ xmlFallback } );

        if ( bSuccess && pOwner != nullptr )
        {
            const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pOwner );
            EditorTransaction::recordModify( pOwner, beforeSnapshot, afterSnapshot, "Paste Component Values" );
        }
        return bSuccess;
    }

    bool EditorTransformCommands::pasteComponentValues( Component* pTargetComp, string_view xml )
    {
        return pasteComponentValues( pTargetComp, vector<uint8>{}, xml );
    }

    Component* EditorTransformCommands::pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, const vector<uint8>& bytes, string_view xmlFallback )
    {
        if ( pTargetObj == nullptr || typeName.empty() )
            return nullptr;

        GameObjectManager* pManager = pTargetObj->getManager();
        if ( pManager == nullptr )
            return nullptr;

        const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pTargetObj );

        Component* pNewComp = pManager->addComponentByName( pTargetObj, hashed_string{ typeName } );
        if ( pNewComp != nullptr && pNewComp->getTypeInfo() != nullptr )
        {
            bool bSuccess = false;
            if ( bytes.empty() == false )
                bSuccess = BinarySerializer::deserialize( pNewComp, *pNewComp->getTypeInfo(), bytes.data(), bytes.size() );

            if ( bSuccess == false && xmlFallback.empty() == false )
                bSuccess = XmlSerializer::deserialize( pNewComp, *pNewComp->getTypeInfo(), string{ xmlFallback } );

            // 예전에는 이 결과를 **아무도 읽지 않았다.** 둘 다 실패해도 값이 하나도 안 들어간
            // 컴포넌트를 붙여 놓고 "붙여넣기" 실행 취소 항목까지 남겨서, 쓰는 사람은 왜 비었는지
            // 알 수 없었다. 컴포넌트는 이미 붙었으니 되돌리지 않고, 대신 조용히 넘어가지 않는다.
            if ( bSuccess == false )
            {
                SW_LOG_WARNING( "Paste Component as New: %# 의 값을 읽지 못했습니다. 빈 컴포넌트가 추가됩니다.",
                                string{ typeName }.c_str() );
            }

            const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pTargetObj );
            EditorTransaction::recordModify( pTargetObj, beforeSnapshot, afterSnapshot, "Paste Component as New" );
            return pNewComp;
        }
        return nullptr;
    }

    Component* EditorTransformCommands::pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, string_view xml )
    {
        return pasteComponentAsNew( pTargetObj, typeName, vector<uint8>{}, xml );
    }

    bool EditorTransformCommands::saveComponentPreset( const Component* pComp, string_view presetName )
    {
        if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || presetName.empty() )
            return false;

        const string presetDir = ResourceUtil::getDomainFolderPath(
            GameConfig::getActive()._packRoot, FileUtil::joinPath( path::kDataFolder, path::kPresetsFolder ) );
        FileUtil::ensureDirectoryExists( presetDir );

        const string compName = pComp->getComponentName().empty() == false ? pComp->getComponentName().c_str()
                                                                           : pComp->getTypeInfo()->_name.c_str();
        const string fileName = compName + "_" + string{ presetName } + ".preset.xml";
        const string fullPath = FileUtil::joinPath( presetDir, fileName );

        const string xmlData = XmlSerializer::serialize( pComp, *pComp->getTypeInfo() );
        return FileUtil::writeTextFile( fullPath, xmlData );
    }

    bool EditorTransformCommands::loadComponentPreset( Component* pComp, string_view presetFilePath )
    {
        if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || presetFilePath.empty() )
            return false;

        string xmlData;
        if ( FileUtil::readTextFile( presetFilePath, xmlData ) == false )
            return false;

        GameObject* const          pOwner         = pComp->getOwner();
        const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pOwner );

        const bool bSuccess = XmlSerializer::deserialize( pComp, *pComp->getTypeInfo(), xmlData );
        if ( bSuccess && pOwner != nullptr )
        {
            const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pOwner );
            EditorTransaction::recordModify( pOwner, beforeSnapshot, afterSnapshot, "Apply Component Preset" );
        }
        return bSuccess;
    }

    void EditorTransformCommands::snapSelectedToGround()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        vector<GameObject*> listSel;
        pContext->getSelectionManager().getSelectedObjects( listSel );
        if ( listSel.empty() )
            return;

        EditorTransaction::beginTransaction( "Snap to Ground" );

        for ( GameObject* pGo : listSel )
        {
            SceneComponent* pSc = pGo->getPrimarySceneComponent();
            if ( pSc == nullptr )
                continue;

            const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pGo );

            float3       pos          = pSc->getWorldPosition();
            const float3 scl          = pSc->getLocalScale();
            float32      bottomOffset = 0.0f;

            BoxCollider2DComponent* pBox = pGo->getComponent<BoxCollider2DComponent>();
            if ( pBox != nullptr )
            {
                const float2 boxScl = pBox->getOffsetScale();
                bottomOffset        = boxScl._y * 0.5f;
            }
            MeshComponent* pMesh = pGo->getComponent<MeshComponent>();
            if ( pMesh != nullptr )
                bottomOffset = scl._y * 0.5f;

            pos._y = bottomOffset;
            pSc->setLocalPosition( pos );

            const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pGo );
            EditorTransaction::recordModify( pGo, beforeSnapshot, afterSnapshot, "Snap to Ground" );
        }

        EditorTransaction::endTransaction();
    }

    void EditorTransformCommands::alignSelectedObjects( AlignAxis axis, AlignType type )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        vector<GameObject*> listSel;
        pContext->getSelectionManager().getSelectedObjects( listSel );
        if ( listSel.size() < 2 )
            return;

        float32 targetVal = 0.0f;
        if ( type == AlignType::Min )
            targetVal = 1e9f;
        else if ( type == AlignType::Max )
            targetVal = -1e9f;

        float32 sumVal     = 0.0f;
        uint32  validCount = 0;

        for ( GameObject* pGo : listSel )
        {
            if ( pGo->getPrimarySceneComponent() == nullptr )
                continue;

            const float32 val = EditorTransformCommandsInternal::worldAxisValue( pGo->getPrimarySceneComponent()->getWorldPosition(), axis );

            if ( type == AlignType::Min )
                targetVal = MathUtil::min( targetVal, val );
            else if ( type == AlignType::Max )
                targetVal = MathUtil::max( targetVal, val );

            sumVal += val;
            validCount++;
        }

        if ( validCount == 0 )
            return;

        if ( type == AlignType::Center )
            targetVal = sumVal / static_cast<float32>( validCount );

        EditorTransaction::beginTransaction( "Align Objects" );

        for ( GameObject* pGo : listSel )
        {
            if ( pGo->getPrimarySceneComponent() == nullptr )
                continue;

            const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pGo );
            SceneComponent*            pSc            = pGo->getPrimarySceneComponent();
            const float3               localPos       = pSc->getLocalPosition();
            const float3               worldPos       = pSc->getWorldPosition();
            const float32              curWorldAxis   = EditorTransformCommandsInternal::worldAxisValue( worldPos, axis );
            const float32              delta          = targetVal - curWorldAxis;
            float3                     newLocal       = localPos;
            EditorTransformCommandsInternal::setLocalAxisValue( newLocal, axis, EditorTransformCommandsInternal::worldAxisValue( localPos, axis ) + delta );
            pSc->setLocalPosition( newLocal );

            const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pGo );
            EditorTransaction::recordModify( pGo, beforeSnapshot, afterSnapshot, "Align Objects" );
        }

        EditorTransaction::endTransaction();
    }

    void EditorTransformCommands::distributeSelectedObjects( AlignAxis axis )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        vector<GameObject*> listSel;
        pContext->getSelectionManager().getSelectedObjects( listSel );

        // primary scene component가 없는 오브젝트는 분배 대상에서 제외합니다(front/back 역참조 보호).
        listSel.erase( std::remove_if( listSel.begin(), listSel.end(),
                                       []( const GameObject* pGo )
        {
            return pGo->getPrimarySceneComponent() == nullptr;
        } ),
                       listSel.end() );
        if ( listSel.size() < 3 )
            return;

        EditorTransformCommandsInternal::AlignSortLess sortLess{};
        sortLess._axis = axis;
        std::sort( listSel.begin(), listSel.end(), sortLess );

        const float3 firstPos = listSel.front()->getPrimarySceneComponent()->getWorldPosition();
        const float3 lastPos  = listSel.back()->getPrimarySceneComponent()->getWorldPosition();

        const float32 minVal = EditorTransformCommandsInternal::worldAxisValue( firstPos, axis );
        const float32 maxVal = EditorTransformCommandsInternal::worldAxisValue( lastPos, axis );
        const float32 step   = ( maxVal - minVal ) / static_cast<float32>( listSel.size() - 1 );

        EditorTransaction::beginTransaction( "Distribute Objects" );

        for ( size_t idx = 0; idx < listSel.size(); ++idx )
        {
            GameObject* pGo = listSel[idx];
            if ( pGo->getPrimarySceneComponent() == nullptr )
                continue;

            const EditorObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pGo );
            SceneComponent*            pSc            = pGo->getPrimarySceneComponent();
            const float3               localPos       = pSc->getLocalPosition();
            const float3               worldPos       = pSc->getWorldPosition();
            const float32              curWorldAxis   = EditorTransformCommandsInternal::worldAxisValue( worldPos, axis );
            const float32              targetDistVal  = minVal + step * static_cast<float32>( idx );
            const float32              delta          = targetDistVal - curWorldAxis;
            float3                     newLocal       = localPos;
            EditorTransformCommandsInternal::setLocalAxisValue( newLocal, axis, EditorTransformCommandsInternal::worldAxisValue( localPos, axis ) + delta );
            pSc->setLocalPosition( newLocal );

            const EditorObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pGo );
            EditorTransaction::recordModify( pGo, beforeSnapshot, afterSnapshot, "Distribute Objects" );
        }

        EditorTransaction::endTransaction();
    }
} // namespace sw::editor
