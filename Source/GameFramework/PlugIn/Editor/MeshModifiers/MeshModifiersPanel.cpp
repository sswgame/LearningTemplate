/**
 * @file MeshModifiersPanel.cpp
 * @brief Mesh Modifiers 패널을 구현합니다. 매개변수 입력칸은 선언 종류(Float · Int · Bool · Axis · Vector3 · Curve · Reference · AssetPath · NumberList)로 고릅니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifiersPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/LatticeModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MeshModifiersPanelInternal
        {
            static constexpr const utf8* kAxisNames = "X\0Y\0Z\0";

            static bool drawTextInput( const utf8* pLabel, const string& value, string& outValue )
            {
                utf8 arrBuffer[constant::kMaxPathSize] = {};
                const size_t copyCount = MathUtil::min( value.size(), static_cast<size_t>( constant::kMaxPathSize - 1 ) );
                for ( size_t index = 0; index < copyCount; ++index )
                {
                    arrBuffer[index] = value[index];
                }
                if ( ImGui::InputText( pLabel, arrBuffer, constant::kMaxPathSize ) == false )
                    return false;
                outValue = string( arrBuffer );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifiersPanel::MeshModifiersPanel()
        : IEditorPanel( false )
        , _selectedControlPoint{ 0 }
        , _bEditing{ false }
        , _bMirrorEdit{ false }
        , _mirrorAxis{ 0 }
    {
    }

    void MeshModifiersPanel::drawContent()
    {
        MeshModifierSession* pSession = getService<MeshModifierSession>();
        if ( pSession == nullptr )
            return; // 이 확장의 서브시스템이 아직 걸리지 않았다(프레임 앞 sync 전)
        MeshModifierSession& session = *pSession;
        drawTargetRow( session );
        if ( session.hasTarget() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdMeshModifiers", "HintPickTarget" ) );
            return;
        }
        if ( session.findTarget() == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdMeshModifiers", "HintTargetGone" ) );
            return;
        }
        ImGui::Separator();
        drawStackList( session );
        ImGui::Separator();
        drawSelectedModifier( session );
        ImGui::Separator();
        drawFooter( session );
        session.refreshPreview();
    }

    void MeshModifiersPanel::drawTargetRow( MeshModifierSession& session )
    {
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "UseSelection" ) ) )
        {
            EditorSelection* pSelection = getService<EditorSelection>();
            GameObject*      pObject    = pSelection != nullptr ? pSelection->getPrimaryObject() : nullptr;
            if ( session.setTarget( pObject ) == false )
                SW_LOG_WARNING( "Mesh modifiers need an object whose mesh component uses a .mesh asset" );
        }
        ImGui::SameLine();
        if ( session.hasTarget() )
            ImGui::TextUnformatted( session.getMeshPath().c_str() );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdMeshModifiers", "NoTarget" ) );
    }

    void MeshModifiersPanel::drawStackList( MeshModifierSession& session )
    {
        MeshModifierStack& stack = session.getStack();
        drawAddMenu( session );

        int32 removeIndex = -1;
        int32 upIndex     = -1;
        int32 downIndex   = -1;
        for ( int32 index = 0; index < stack.getModifierCount(); ++index )
        {
            MeshModifier* pModifier = stack.findModifier( index );
            ImGui::PushID( index );
            bool bEnabled = pModifier->isEnabled();
            if ( ImGui::Checkbox( "##Enabled", &bEnabled ) )
            {
                session.recordUndo();
                pModifier->setEnabled( bEnabled );
                session.markDirty();
            }
            ImGui::SameLine();
            if ( ImGui::Selectable( pModifier->getName().c_str(), session.getSelectedIndex() == index, ImGuiSelectableFlags_None,
                                    ImVec2{ ImGui::GetContentRegionAvail().x - 90.0f, 0.0f } ) )
                session.setSelectedIndex( index );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "^" ) )
                upIndex = index;
            ImGui::SameLine();
            if ( ImGui::SmallButton( "v" ) )
                downIndex = index;
            ImGui::SameLine();
            if ( ImGui::SmallButton( "x" ) )
                removeIndex = index;
            ImGui::PopID();
        }

        // 목록을 다 그린 뒤 바꾼다(그리는 도중에 지우면 포인터가 무효가 된다)
        if ( upIndex >= 0 )
        {
            session.recordUndo();
            if ( stack.moveUp( upIndex ) )
                session.setSelectedIndex( upIndex - 1 );
            session.markDirty();
        }
        if ( downIndex >= 0 )
        {
            session.recordUndo();
            if ( stack.moveDown( downIndex ) )
                session.setSelectedIndex( downIndex + 1 );
            session.markDirty();
        }
        if ( removeIndex >= 0 )
        {
            session.recordUndo();
            (void)stack.removeModifier( removeIndex ); // 방금 그린 번호라 범위 안이다
            session.setSelectedIndex( MathUtil::min( session.getSelectedIndex(), stack.getModifierCount() - 1 ) );
            session.markDirty();
        }
    }

    void MeshModifiersPanel::drawAddMenu( MeshModifierSession& session )
    {
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "AddModifier" ) ) )
            ImGui::OpenPopup( "##AddModifier" );
        if ( ImGui::BeginPopup( "##AddModifier" ) == false )
            return;
        vector<const MeshModifierRegistration*> listRegistration;
        MeshModifierRegistry::getRegistrations( listRegistration );
        size_t index = 0;
        while ( index < listRegistration.size() )
        {
            // 레지스트리가 카테고리 순으로 주므로 같은 카테고리를 한 하위 메뉴로 묶는다
            const string_view category = listRegistration[index]->_pCategory;
            size_t            end      = index;
            while ( end < listRegistration.size() && string_view( listRegistration[end]->_pCategory ) == category )
            {
                ++end;
            }
            if ( ImGui::BeginMenu( listRegistration[index]->_pCategory ) )
            {
                for ( size_t item = index; item < end; ++item )
                {
                    if ( ImGui::MenuItem( listRegistration[item]->_pDisplayName ) == false )
                        continue;
                    session.recordUndo();
                    MeshModifierStack& stack    = session.getStack();
                    const int32        newIndex = stack.addModifier( listRegistration[item]->_pID );
                    MeshModifier*      pAdded   = stack.findModifier( newIndex );
                    if ( pAdded != nullptr )
                        pAdded->fitToBounds( session.getSourceBounds() );
                    session.setSelectedIndex( newIndex );
                    session.markDirty();
                }
                ImGui::EndMenu();
            }
            index = end;
        }
        ImGui::EndPopup();
    }

    void MeshModifiersPanel::drawSelectedModifier( MeshModifierSession& session )
    {
        MeshModifier* pModifier = session.getStack().findModifier( session.getSelectedIndex() );
        if ( pModifier == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdMeshModifiers", "HintSelectModifier" ) );
            return;
        }
        ImGui::PushID( pModifier );
        string name = pModifier->getName();
        if ( MeshModifiersPanelInternal::drawTextInput( SW_LOCKEY( "EdMeshModifiers", "Name" ), name, name ) )
        {
            noteEdit( session );
            pModifier->setName( name );
        }
        finishItem();
        ImGui::TextDisabled( "%s", pModifier->getTypeID().c_str() );
        if ( pModifier->supportsGPU() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdMeshModifiers", "CPUOnly" ) );
        }

        drawGizmo( session, *pModifier );
        drawParams( session, *pModifier );
        LatticeModifier* pLattice = LatticeModifier::findFrom( *pModifier );
        if ( pLattice != nullptr )
            drawLatticePoints( session, *pLattice );
        ImGui::PopID();
    }

    void MeshModifiersPanel::drawGizmo( MeshModifierSession& session, MeshModifier& modifier )
    {
        MeshModifierGizmo& gizmo  = modifier.getGizmo();
        float3             center = gizmo._center;
        if ( ImGui::DragFloat3( SW_LOCKEY( "EdMeshModifiers", "Center" ), &center._x, 0.01f ) )
        {
            noteEdit( session );
            gizmo._center = center;
        }
        finishItem();
        float3 rotation = gizmo._rotationDegree;
        if ( ImGui::DragFloat3( SW_LOCKEY( "EdMeshModifiers", "Rotation" ), &rotation._x, 0.5f ) )
        {
            noteEdit( session );
            gizmo._rotationDegree = rotation;
        }
        finishItem();
        int32 axis = static_cast<int32>( gizmo._axisIndex );
        if ( ImGui::Combo( SW_LOCKEY( "EdMeshModifiers", "Axis" ), &axis, MeshModifiersPanelInternal::kAxisNames ) )
        {
            session.recordUndo();
            gizmo._axisIndex = static_cast<uint32>( MathUtil::clamp( axis, 0, 2 ) );
            session.markDirty();
        }
        bool bWorldSpace = modifier.isWorldSpace();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "WorldSpace" ), &bWorldSpace ) )
        {
            session.recordUndo();
            modifier.setWorldSpace( bWorldSpace );
            session.markDirty();
        }
        if ( modifier.isWorldSpace() )
        {
            string reference = modifier.getSpaceReference();
            if ( MeshModifiersPanelInternal::drawTextInput( SW_LOCKEY( "EdMeshModifiers", "SpaceReference" ), reference, reference ) )
            {
                noteEdit( session );
                modifier.setSpaceReference( reference );
            }
            finishItem();
        }
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "FitBounds" ) ) )
        {
            session.recordUndo();
            modifier.fitToBounds( session.getSourceBounds() );
            session.markDirty();
        }
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "ResetParams" ) ) )
        {
            session.recordUndo();
            modifier.resetParams();
            session.markDirty();
        }
    }

    void MeshModifiersPanel::drawParams( MeshModifierSession& session, MeshModifier& modifier )
    {
        const vector<MeshModifierParamDef>& listDef = modifier.getParamDefs();
        for ( uint32 paramIndex = 0; paramIndex < modifier.getParamCount(); ++paramIndex )
        {
            const MeshModifierParamDef& def = listDef[paramIndex];
            ImGui::PushID( static_cast<int32>( paramIndex ) );
            switch ( def._kind )
            {
                case MeshModifierParamKind::Float:
                {
                    float32 value = modifier.getFloat( paramIndex );
                    if ( ImGui::DragFloat( def._pName, &value, MathUtil::max( ( def._max - def._min ) * 0.001f, 0.0001f ), def._min, def._max ) )
                    {
                        noteEdit( session );
                        modifier.setScalar( paramIndex, value );
                        modifier.onParamsChanged();
                    }
                    finishItem();
                    break;
                }
                case MeshModifierParamKind::Int:
                {
                    int32 value = modifier.getInt( paramIndex );
                    if ( ImGui::DragInt( def._pName, &value, 0.1f, static_cast<int32>( def._min ), static_cast<int32>( def._max ) ) )
                    {
                        noteEdit( session );
                        modifier.setScalar( paramIndex, static_cast<float32>( value ) );
                        modifier.onParamsChanged();
                    }
                    finishItem();
                    break;
                }
                case MeshModifierParamKind::Bool:
                {
                    bool bValue = modifier.getBool( paramIndex );
                    if ( ImGui::Checkbox( def._pName, &bValue ) )
                    {
                        session.recordUndo();
                        modifier.setScalar( paramIndex, bValue ? 1.0f : 0.0f );
                        modifier.onParamsChanged();
                        session.markDirty();
                    }
                    break;
                }
                case MeshModifierParamKind::Axis:
                {
                    int32 axis = static_cast<int32>( modifier.getAxisIndex( paramIndex ) );
                    if ( ImGui::Combo( def._pName, &axis, MeshModifiersPanelInternal::kAxisNames ) )
                    {
                        session.recordUndo();
                        modifier.setScalar( paramIndex, static_cast<float32>( axis ) );
                        modifier.onParamsChanged();
                        session.markDirty();
                    }
                    break;
                }
                case MeshModifierParamKind::Vector3:
                {
                    float3 value = modifier.getVector3( paramIndex );
                    if ( ImGui::DragFloat3( def._pName, &value._x, 0.01f, def._min, def._max ) )
                    {
                        noteEdit( session );
                        modifier.setVector3( paramIndex, value );
                        modifier.onParamsChanged();
                    }
                    finishItem();
                    break;
                }
                case MeshModifierParamKind::Curve:
                {
                    ImGui::TextUnformatted( def._pName );
                    vector<float2> listKey  = modifier.getCurveKeys( paramIndex );
                    bool           bChanged = false;
                    int32          removeAt = -1;
                    for ( size_t keyIndex = 0; keyIndex < listKey.size(); ++keyIndex )
                    {
                        ImGui::PushID( static_cast<int32>( keyIndex ) );
                        if ( ImGui::DragFloat2( "##Key", &listKey[keyIndex]._x, 0.005f ) )
                        {
                            noteEdit( session );
                            bChanged = true;
                        }
                        finishItem();
                        ImGui::SameLine();
                        if ( listKey.size() > 1 && ImGui::SmallButton( "x" ) )
                            removeAt = static_cast<int32>( keyIndex );
                        ImGui::PopID();
                    }
                    if ( ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "AddKey" ) ) )
                    {
                        session.recordUndo();
                        listKey.push_back( listKey.empty() ? float2{ 0.0f, 0.0f } : float2{ 1.0f, listKey.back()._y } );
                        bChanged = true;
                    }
                    if ( removeAt >= 0 )
                    {
                        session.recordUndo();
                        listKey.erase( listKey.begin() + removeAt );
                        bChanged = true;
                    }
                    if ( bChanged )
                    {
                        modifier.setCurveKeys( paramIndex, listKey );
                        modifier.onParamsChanged();
                        session.markDirty();
                    }
                    break;
                }
                case MeshModifierParamKind::Reference:
                case MeshModifierParamKind::AssetPath:
                {
                    string text = modifier.getText( paramIndex );
                    if ( MeshModifiersPanelInternal::drawTextInput( def._pName, text, text ) )
                    {
                        noteEdit( session );
                        modifier.setText( paramIndex, text );
                        modifier.onParamsChanged();
                    }
                    finishItem();
                    if ( def._pHint != nullptr && ImGui::IsItemHovered() )
                        ImGui::SetTooltip( "%s", def._pHint );
                    break;
                }
                case MeshModifierParamKind::NumberList:
                {
                    ImGui::TextUnformatted( def._pName );
                    if ( def._pHint != nullptr )
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled( "(%s)", def._pHint );
                    }
                    vector<float32> listNumber = modifier.getNumberList( paramIndex );
                    const size_t    stride     = MathUtil::max<size_t>( def._stride, 1 );
                    bool            bChanged   = false;
                    for ( size_t number = 0; number < listNumber.size(); ++number )
                    {
                        ImGui::PushID( static_cast<int32>( number ) );
                        if ( number % stride != 0 )
                            ImGui::SameLine();
                        ImGui::SetNextItemWidth( 60.0f );
                        if ( ImGui::DragFloat( "##Number", &listNumber[number], 0.01f, def._min, def._max ) )
                        {
                            noteEdit( session );
                            bChanged = true;
                        }
                        finishItem();
                        ImGui::PopID();
                    }
                    if ( ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "AddItem" ) ) )
                    {
                        session.recordUndo();
                        listNumber.insert( listNumber.end(), stride, 0.0f );
                        bChanged = true;
                    }
                    ImGui::SameLine();
                    if ( listNumber.size() >= stride && ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "RemoveItem" ) ) )
                    {
                        session.recordUndo();
                        listNumber.resize( listNumber.size() - stride );
                        bChanged = true;
                    }
                    if ( bChanged )
                    {
                        modifier.setNumberList( paramIndex, listNumber );
                        modifier.onParamsChanged();
                        session.markDirty();
                    }
                    break;
                }
            }
            ImGui::PopID();
        }
    }

    void MeshModifiersPanel::drawLatticePoints( MeshModifierSession& session, LatticeModifier& lattice )
    {
        // 뷰포트에서 점을 끄는 도구 모드 대신 패널 입력칸으로 제어점을 옮긴다 — 시각화가 고른 점을 강조해 보인다
        ImGui::SeparatorText( SW_LOCKEY( "EdMeshModifiers", "ControlPoints" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "ResetLattice" ) ) )
        {
            session.recordUndo();
            lattice.resetToIdentity();
            session.markDirty();
        }
        ImGui::SameLine();
        ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "Mirror" ), &_bMirrorEdit );
        if ( _bMirrorEdit )
        {
            ImGui::SameLine();
            int32 axis = _mirrorAxis;
            ImGui::SetNextItemWidth( 60.0f );
            if ( ImGui::Combo( "##MirrorAxis", &axis, MeshModifiersPanelInternal::kAxisNames ) )
                _mirrorAxis = static_cast<uint8>( MathUtil::clamp( axis, 0, 2 ) );
        }

        const int32 count     = lattice.getControlPointCount();
        _selectedControlPoint = MathUtil::clamp( _selectedControlPoint, 0, MathUtil::max( count - 1, 0 ) );
        if ( ImGui::SliderInt( SW_LOCKEY( "EdMeshModifiers", "PointIndex" ), &_selectedControlPoint, 0, MathUtil::max( count - 1, 0 ) ) )
            session.markDirty();
        if ( count <= 0 )
            return;
        float3 position = lattice.getControlPoint( _selectedControlPoint );
        if ( ImGui::DragFloat3( SW_LOCKEY( "EdMeshModifiers", "PointPosition" ), &position._x, 0.005f ) )
        {
            noteEdit( session );
            const float3        delta = position - lattice.getControlPoint( _selectedControlPoint );
            const vector<int32> listIndex{ _selectedControlPoint };
            lattice.moveControlPoints( listIndex, delta, _bMirrorEdit, _mirrorAxis );
        }
        finishItem();
    }

    void MeshModifiersPanel::drawFooter( MeshModifierSession& session )
    {
        MeshModifierStack& stack       = session.getStack();
        bool               bRecompute  = stack.isRecomputingNormals();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "RecomputeNormals" ), &bRecompute ) )
        {
            session.recordUndo();
            stack.setRecomputingNormals( bRecompute );
            session.markDirty();
        }
        float32 time = session.getPreviewTime();
        if ( ImGui::DragFloat( SW_LOCKEY( "EdMeshModifiers", "PreviewTime" ), &time, 0.01f, 0.0f, 600.0f ) )
            session.setPreviewTime( time );

        ImGui::BeginDisabled( session.canUndo() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kUndo, SW_LOCKEY( "EdMeshModifiers", "Undo" ) ) ) )
            (void)session.undo(); // 버튼이 켜져 있으면 기록이 있다
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( session.canRedo() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRedo, SW_LOCKEY( "EdMeshModifiers", "Redo" ) ) ) )
            (void)session.redo(); // 버튼이 켜져 있으면 기록이 있다
        ImGui::EndDisabled();

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kSave, SW_LOCKEY( "EdMeshModifiers", "SaveStack" ) ) ) )
            (void)session.saveSidecar(); // 실패는 saveSidecar 가 알린다
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "ReloadStack" ) ) )
        {
            session.recordUndo();
            if ( session.loadSidecar() == false )
                SW_LOG_WARNING( "No mesh modifier stack next to %#", session.getMeshPath().c_str() );
        }
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "Bake" ) ) )
        {
            string bakedPath;
            (void)session.bake( &bakedPath ); // 성공 · 실패 모두 bake 가 알린다
        }
    }

    void MeshModifiersPanel::noteEdit( MeshModifierSession& session )
    {
        if ( _bEditing == false )
            session.recordUndo();
        _bEditing = ImGui::IsItemActive();
        session.markDirty();
    }

    void MeshModifiersPanel::finishItem()
    {
        if ( ImGui::IsItemDeactivated() )
            _bEditing = false;
    }

    SW_EDITOR_PANEL( MeshModifiersPanel, "meshmodifiers.stack", EditorPanelCategory::Custom, 9010 );
} // namespace sw::editor
