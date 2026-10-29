#include "pch.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Model/MeshModifiersPanelModel.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Modifiers/DeformableModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Modifiers/WorldWarpModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifierStack.h"

namespace sw::editor
{
    SW_LOG_CALLER( "MeshModifiers" );

    MeshModifiersPanelModel::MeshModifiersPanelModel()
        : _listNestedPath{}
        , _nestedRootID{ 0 }
        , _nestedSelectedIndex{ -1 }
        , _enterRequest{ -1 }
        , _bLeaveRequested{ false }
        , _curveModifierID{ 0 }
        , _curveParamIndex{ 0 }
        , _curveItemIndex{ -1 }
        , _descriptionID{}
        , _description{}
        , _warpExportPath{}
        , _bEditing{ false }
    {
    }

    MeshModifierStack& MeshModifiersPanelModel::resolveNestedStack( DeformableModifier& root )
    {
        _enterRequest    = -1;
        _bLeaveRequested = false;
        if ( _nestedRootID != root.getInstanceID() )
        {
            // 다른 묶음을 골랐다(되돌리기로 다시 지은 같은 묶음도 새 인스턴스다) - 하위 스택의 고른 자리를 처음으로 되돌린다
            _nestedRootID = root.getInstanceID();
            _listNestedPath.clear();
            _nestedSelectedIndex = -1;
        }
        // 보여 줄 스택 = 경로가 가리키는 묶음의 하위 스택(빈 경로면 고른 묶음 자신의 하위 스택). 경로가 무효가 되면 처음으로
        if ( _listNestedPath.empty() )
            return root.getStack();
        MeshModifier*       pOwner = DeformableModifier::findByPath( root.getStack(), _listNestedPath );
        DeformableModifier* pGroup = pOwner != nullptr ? DeformableModifier::findFrom( *pOwner ) : nullptr;
        if ( pGroup != nullptr )
            return pGroup->getStack();
        _listNestedPath.clear();
        _nestedSelectedIndex = -1;
        return root.getStack();
    }

    void MeshModifiersPanelModel::applyNestedRequest()
    {
        if ( _enterRequest >= 0 )
        {
            _listNestedPath.push_back( _enterRequest );
            _nestedSelectedIndex = -1;
        }
        else if ( _bLeaveRequested && _listNestedPath.empty() == false )
        {
            _nestedSelectedIndex = _listNestedPath.back();
            _listNestedPath.pop_back();
        }
        _enterRequest    = -1;
        _bLeaveRequested = false;
    }

    void MeshModifiersPanelModel::openCurvePopup( const MeshModifier& modifier, uint32 paramIndex, int32 itemIndex )
    {
        _curveModifierID = modifier.getInstanceID();
        _curveParamIndex = paramIndex;
        _curveItemIndex  = itemIndex;
    }

    bool MeshModifiersPanelModel::isCurvePopupFor( const MeshModifier& modifier, uint32 paramIndex, int32 itemIndex ) const
    {
        return _curveModifierID != 0 && _curveModifierID == modifier.getInstanceID() && _curveParamIndex == paramIndex && _curveItemIndex == itemIndex;
    }

    const string& MeshModifiersPanelModel::findDescription( const MeshModifierRegistration& registration )
    {
        if ( string_view( _descriptionID ) != string_view( registration._pID ) )
        {
            // 설명은 인스턴스의 가상 함수라 하나 만들어 묻는다 — 마우스가 새 항목에 올 때 한 번뿐이다
            _descriptionID                           = string( registration._pID );
            const unique_ptr<MeshModifier> pModifier = registration._pCreateFn();
            _description                             = pModifier != nullptr ? string( pModifier->getDescription() ) : string();
        }
        return _description;
    }

    bool MeshModifiersPanelModel::canExportWarp( const MeshModifierStack& shown ) const
    {
        // 경로는 사용자가 정한다(확장자가 `.mfwarp` 가 아니면 단추를 끈다 — 에셋 슬롯 필터가 그 확장자만 고른다).
        const string_view extension     = WorldWarpModifier::kFileExtension;
        const bool        bWarpFilePath = _warpExportPath.size() > extension.size() &&
                                   string_view( _warpExportPath ).substr( _warpExportPath.size() - extension.size() ) == extension;
        return bWarpFilePath && shown.getModifierCount() > 0;
    }

    void MeshModifiersPanelModel::exportWarp( const MeshModifierStack& shown ) const
    {
        if ( WorldWarpModifier::writeWarpFile( shown, _warpExportPath ) )
            SW_LOG_INFO( "Exported %# modifiers as the world warp '%#'", shown.getModifierCount(), _warpExportPath.c_str() );
        else
            SW_LOG_ERROR( "Could not write the world warp '%#'", _warpExportPath.c_str() );
    }

    void MeshModifiersPanelModel::noteEdit( MeshModifierSession& session, bool bItemActive )
    {
        if ( _bEditing == false )
            session.recordUndo();
        _bEditing = bItemActive;
        session.markDirty();
    }
} // namespace sw::editor
