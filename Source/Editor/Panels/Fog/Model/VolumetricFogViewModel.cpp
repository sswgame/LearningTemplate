#include "pch.h"

#include "Editor/Panels/Fog/Model/VolumetricFogViewModel.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include "Editor/Common/EditorSceneWalk.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/EnumName.h"

namespace sw::editor
{
    namespace
    {
        struct VolumetricFogViewModelInternal
        {
            static constexpr const utf8* kViewVariable  = "gv_fogDebugView";  ///< 안개 패스의 진단 보기(개발 도구 `RenderDiagnosticsSwitches.cpp` 가 진단 정책으로 넘긴다)
            static constexpr const utf8* kSliceVariable = "gv_fogDebugSlice"; ///< 진단 단면의 거리 조각

            /** @brief 전역 변수의 지금 글입니다. 등록되지 않았으면(Shipping 이 아닌데 없으면 결함) 빈 글입니다. */
            static string readVariable( const utf8* pName )
            {
                const GlobalVariableInfo* pInfo = editor::getSubsystem<GlobalVariableManager>().findVariable( pName );
                return pInfo != nullptr ? pInfo->getValueAsString() : string{};
            }

            static void writeVariable( const utf8* pName, string_view value )
            {
                if ( editor::getSubsystem<GlobalVariableManager>().setValueFromString( pName, value ) == false )
                    SW_LOG_WARNING_ID( "VolumetricFogPanel.VariableNotSet", "The fog debug variable '%#' did not take '%#'", pName, value );
            }
        };
    } // namespace

    void VolumetricFogViewModel::gather( VolumetricFogViewState& outState ) const
    {
        using Internal        = VolumetricFogViewModelInternal;
        outState._volumeState = FrameFog::getLastMainVolumeState();
        outState._debugView   = Internal::readVariable( Internal::kViewVariable );
        outState._slice       = 0;
        (void)StringUtil::parseInt( string_view( Internal::readVariable( Internal::kSliceVariable ) ), outState._slice ); // 등록한 int 변수의 글이다
        outState._fogObjectID = 0;
        outState._fogObjectName.clear();

        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        GameObject* pFog = nullptr;
        // 공유 잠금 안의 콜백이라 찾기만 한다.
        pManager->forEachComponentOfType<FogComponent>( editor::EditorSceneWalk::getKey(), [&pFog]( FogComponent* pComponent )
        {
            if ( pFog == nullptr )
                pFog = pComponent->getOwner();
        } );
        if ( pFog == nullptr )
            return;
        outState._fogObjectID   = pFog->getObjectID();
        outState._fogObjectName = pFog->getName().c_str();
    }

    void VolumetricFogViewModel::setDebugView( FogDebugView view )
    {
        using Internal = VolumetricFogViewModelInternal;
        Internal::writeVariable( Internal::kViewVariable, getEnumName( view ) );
    }

    void VolumetricFogViewModel::setSlice( int32 slice )
    {
        using Internal = VolumetricFogViewModelInternal;
        Internal::writeVariable( Internal::kSliceVariable, to_string( slice ) );
    }

    void VolumetricFogViewModel::selectFogObject( uint64 objectID )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pFog     = pManager != nullptr && objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
        if ( pFog != nullptr )
            editor::getMainSceneSelection().selectObject( pFog );
    }
} // namespace sw::editor
