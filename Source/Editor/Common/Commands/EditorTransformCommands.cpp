#include "pch.h"

#include "Editor/Common/Commands/EditorTransformCommands.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/AABB.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTransformCommandsInternal
        {
            /** @brief 컴포넌트 프리셋 파일의 접미사입니다. */
            static constexpr string_view kPresetSuffix = ".preset.xml";

            static float32 getAxisValue( const float3& pos, AlignAxis axis )
            {
                if ( axis == AlignAxis::X )
                    return pos._x;
                if ( axis == AlignAxis::Y )
                    return pos._y;
                return pos._z;
            }

            static void setAxisValue( float3& pos, AlignAxis axis, float32 value )
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
                    return getAxisValue( posA, _axis ) < getAxisValue( posB, _axis );
                }
            };

            /**
             * @brief 오브젝트를 월드 축 `axis` 의 값이 `targetValue` 가 되도록 옮기고 되돌리기 기록을 남깁니다. 정렬 · 분배가 함께 씁니다.
             * @details 월드 위치를 바꿔 쓴다(`setWorldPosition`). 월드 차이만큼 **로컬** 위치를 옮기면 부모에 회전 · 크기가 있을 때 틀린다.
             *          주 씬 컴포넌트가 없으면 아무것도 하지 않습니다.
             */
            static void moveAlongWorldAxis( GameObject* pGo, AlignAxis axis, float32 targetValue, string_view actionName )
            {
                SceneComponent* pSc = pGo->getPrimarySceneComponent();
                if ( pSc == nullptr )
                    return;

                const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pGo );
                float3               worldPosition  = pSc->getWorldPosition();
                setAxisValue( worldPosition, axis, targetValue );
                pSc->setWorldPosition( worldPosition );

                const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pGo );
                EditorTransaction::recordModify( pGo, beforeSnapshot, afterSnapshot, actionName );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorTransformCommands::pasteComponentValues( Component* pTargetComp, const vector<uint8>& bytes, string_view xmlFallback )
    {
        if ( pTargetComp == nullptr || pTargetComp->getTypeInfo() == nullptr )
            return false;

        GameObject* const    pOwner         = pTargetComp->getOwner();
        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pOwner );
        const hashed_string  targetName     = pTargetComp->getComponentName();

        bool bSuccess = false;
        if ( bytes.empty() == false )
            bSuccess = BinarySerializer::deserialize( pTargetComp, *pTargetComp->getTypeInfo(), bytes.data(), bytes.size() );

        if ( bSuccess == false && xmlFallback.empty() == false )
            bSuccess = XMLSerializer::deserialize( pTargetComp, *pTargetComp->getTypeInfo(), string{ xmlFallback } );
        // 이름표는 값이 아니라 정체다(컴포넌트 키) — 붙여 넣어도 대상의 것을 지킨다(언리얼의 속성 붙여넣기도 컴포넌트 이름을 옮기지 않는다).
        pTargetComp->setComponentName( targetName );

        // 직렬화기는 값만 쓴다 — 컴포넌트가 그 값으로 다시 맞추게 알린다. 알리지 않으면 붙여 넣은 위치가 월드 행렬에 들지 않고
        // (화면 · 기즈모가 옛 자리) 메시 id 를 붙여 넣어도 옛 메시를 그린다.
        if ( bSuccess )
            pTargetComp->notifyStateWritten();

        if ( bSuccess && pOwner != nullptr )
        {
            const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pOwner );
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

        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pTargetObj );

        Component* pNewComp = pManager->addComponentByName( pTargetObj, hashed_string{ typeName } );
        if ( pNewComp != nullptr && pNewComp->getTypeInfo() != nullptr )
        {
            const hashed_string newName  = pNewComp->getComponentName();
            bool                bSuccess = false;
            if ( bytes.empty() == false )
                bSuccess = BinarySerializer::deserialize( pNewComp, *pNewComp->getTypeInfo(), bytes.data(), bytes.size() );

            if ( bSuccess == false && xmlFallback.empty() == false )
                bSuccess = XMLSerializer::deserialize( pNewComp, *pNewComp->getTypeInfo(), string{ xmlFallback } );
            pNewComp->setComponentName( newName ); // 새 컴포넌트는 제 이름표(타입 이름)로 — 원본의 이름표를 옮기면 키가 겹친다
            // 읽지 못했어도 기본값이 그 상태다 — 어느 쪽이든 컴포넌트가 값을 자원으로 바꾸게 한다(`EditorSceneCommands::addComponent` 와 같다).
            pNewComp->notifyStateWritten();

            // 이 결과를 버리면 둘 다 실패해도 값이 하나도 안 들어간 컴포넌트와 "붙여넣기" 실행 취소 항목만
            // 남아, 쓰는 사람은 왜 비었는지 알 수 없다. 컴포넌트는 이미 붙었으니 되돌리지 않고, 대신 조용히 넘어가지 않는다.
            if ( bSuccess == false )
            {
                SW_LOG_WARNING( "Paste Component as New: %# 의 값을 읽지 못했습니다. 빈 컴포넌트가 추가됩니다.",
                                string{ typeName }.c_str() );
            }

            const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pTargetObj );
            EditorTransaction::recordModify( pTargetObj, beforeSnapshot, afterSnapshot, "Paste Component as New" );
            return pNewComp;
        }
        return nullptr;
    }

    Component* EditorTransformCommands::pasteComponentAsNew( GameObject* pTargetObj, string_view typeName, string_view xml )
    {
        return pasteComponentAsNew( pTargetObj, typeName, vector<uint8>{}, xml );
    }

    string EditorTransformCommands::makeComponentPresetFileName( const Component* pComp, string_view presetName )
    {
        if ( pComp == nullptr || presetName.empty() )
            return {};
        return string( pComp->getTypeName().c_str() ) + "_" + string{ presetName } + string( EditorTransformCommandsInternal::kPresetSuffix );
    }

    string EditorTransformCommands::getComponentPresetName( const Component* pComp, string_view presetFilePath )
    {
        if ( pComp == nullptr )
            return {};
        const string fileName = FileUtil::getFileNamePart( presetFilePath );
        const string prefix   = string( pComp->getTypeName().c_str() ) + "_";
        if ( StringUtil::startsWith( fileName, prefix ) == false || StringUtil::endsWith( fileName, EditorTransformCommandsInternal::kPresetSuffix, true ) == false ||
             fileName.size() <= prefix.size() + EditorTransformCommandsInternal::kPresetSuffix.size() )
            return {};
        return fileName.substr( prefix.size(), fileName.size() - prefix.size() - EditorTransformCommandsInternal::kPresetSuffix.size() );
    }

    bool EditorTransformCommands::saveComponentPreset( const Component* pComp, string_view presetName )
    {
        const string folder   = EditorGlobalVariableCommands::getComponentPresetFolderPath();
        const string fileName = makeComponentPresetFileName( pComp, presetName );
        if ( folder.empty() || fileName.empty() )
        {
            SW_LOG_ERROR( "Component preset '%#': the preset folder of the active game could not be resolved", presetName );
            return false;
        }
        return saveComponentPresetTo( pComp, FileUtil::joinPath( folder, fileName ) );
    }

    bool EditorTransformCommands::saveComponentPresetTo( const Component* pComp, string_view filePath )
    {
        if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || filePath.empty() )
            return false;

        string fullPath( filePath );
        if ( StringUtil::endsWith( fullPath, EditorTransformCommandsInternal::kPresetSuffix, true ) == false )
            fullPath = FileUtil::removeExtension( fullPath ) + string( EditorTransformCommandsInternal::kPresetSuffix );
        FileUtil::ensureParentDirectoryExists( fullPath );

        const string xmlData = XMLSerializer::serialize( pComp, *pComp->getTypeInfo() );
        return FileUtil::writeTextFile( fullPath, xmlData );
    }

    bool EditorTransformCommands::loadComponentPreset( Component* pComp, string_view presetFilePath )
    {
        if ( pComp == nullptr || pComp->getTypeInfo() == nullptr || presetFilePath.empty() )
            return false;

        string xmlData;
        if ( FileUtil::readTextFile( presetFilePath, xmlData ) == false )
            return false;

        GameObject* const    pOwner         = pComp->getOwner();
        const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pOwner );

        const hashed_string targetName = pComp->getComponentName();
        const bool          bSuccess   = XMLSerializer::deserialize( pComp, *pComp->getTypeInfo(), xmlData );
        pComp->setComponentName( targetName ); // 프리셋은 값이다 — 이름표(컴포넌트 키)는 대상의 것을 지킨다
        if ( bSuccess )
            pComp->notifyStateWritten();
        if ( bSuccess && pOwner != nullptr )
        {
            const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pOwner );
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
        pContext->getEditorSelection().getSelectedObjects( listSel );
        snapObjectsToGround( listSel );
    }

    void EditorTransformCommands::snapObjectsToGround( const vector<GameObject*>& listObject )
    {
        if ( listObject.empty() )
            return;

        EditorTransaction::beginTransaction( "Snap to Ground" );

        for ( GameObject* pGo : listObject )
        {
            SceneComponent* pSc = pGo->getPrimarySceneComponent();
            if ( pSc == nullptr )
                continue;

            const ObjectSnapshot beforeSnapshot = EditorTransaction::captureSnapshot( pGo );

            // 월드로 읽고 월드로 쓴다. 크기는 오브젝트의 월드 상자 하나로 잰다(`GameObject::getWorldBox`) — 종류마다 따로 셈하면("메시면 월드
            // 스케일 × 단위 상자") 단위 상자가 아닌 메시(구 · 캡슐 · 평면)와 키운 콜라이더가 떠 있거나 파묻힌다.
            float3  pos          = pSc->getWorldPosition();
            float32 bottomOffset = 0.0f;
            AABB    box{};
            if ( pGo->getWorldBox( box ) )
                bottomOffset = pos._y - box._min._y;

            pos._y = bottomOffset;
            pSc->setWorldPosition( pos );

            const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pGo );
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
        pContext->getEditorSelection().getSelectedObjects( listSel );
        alignObjects( listSel, axis, type );
    }

    void EditorTransformCommands::alignObjects( const vector<GameObject*>& listSel, AlignAxis axis, AlignType type )
    {
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

            const float32 val = EditorTransformCommandsInternal::getAxisValue( pGo->getPrimarySceneComponent()->getWorldPosition(), axis );

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
            EditorTransformCommandsInternal::moveAlongWorldAxis( pGo, axis, targetVal, "Align Objects" );
        }
        EditorTransaction::endTransaction();
    }

    void EditorTransformCommands::distributeSelectedObjects( AlignAxis axis )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        vector<GameObject*> listSel;
        pContext->getEditorSelection().getSelectedObjects( listSel );
        distributeObjects( std::move( listSel ), axis );
    }

    void EditorTransformCommands::distributeObjects( vector<GameObject*> listSel, AlignAxis axis )
    {
        // 주 SceneComponent 가 없는 오브젝트는 분배 대상에서 뺀다(front/back 역참조 보호).
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

        const float32 minVal = EditorTransformCommandsInternal::getAxisValue( firstPos, axis );
        const float32 maxVal = EditorTransformCommandsInternal::getAxisValue( lastPos, axis );
        const float32 step   = ( maxVal - minVal ) / static_cast<float32>( listSel.size() - 1 );

        EditorTransaction::beginTransaction( "Distribute Objects" );
        for ( size_t idx = 0; idx < listSel.size(); ++idx )
        {
            const float32 targetValue = minVal + step * static_cast<float32>( idx );
            EditorTransformCommandsInternal::moveAlongWorldAxis( listSel[idx], axis, targetValue, "Distribute Objects" );
        }
        EditorTransaction::endTransaction();
    }
} // namespace sw::editor
