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
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/FloatCurve.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/LatticeModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierParamEdit.h"
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

            /** @brief 일반 숫자 표의 열 머리(원소 번호)입니다. 넘치는 열은 이름을 비웁니다. */
            static const utf8* getGenericColumnName( uint32 column )
            {
                static constexpr const utf8* kArrName[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" };
                return column < static_cast<uint32>( sizeof( kArrName ) / sizeof( kArrName[0] ) ) ? kArrName[column] : "";
            }

            /** @brief 단위가 있으면 "%.3f deg" 처럼 값 서식 뒤에 붙입니다. 단위 글의 '%' 는 쓰지 않습니다(영문 단위 이름만). */
            static string makeFormat( const utf8* pBaseFormat, const utf8* pUnit )
            {
                string format( pBaseFormat );
                if ( pUnit != nullptr && pUnit[0] != '\0' )
                {
                    format += " ";
                    format += string_view( pUnit );
                }
                return format;
            }

            /** @brief 직전 항목에 마우스가 있으면 툴팁을 보입니다. */
            static void drawItemTooltip( const utf8* pTooltip )
            {
                if ( pTooltip != nullptr && pTooltip[0] != '\0' && ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "%s", pTooltip );
            }

            /** @brief 오브젝트가 Reference 걸러 보기(컴포넌트 타입 이름)를 통과하면 true 입니다. 걸러 보기가 없으면 늘 true 입니다. */
            static bool matchesComponentHint( const GameObject& object, const utf8* pComponentHint )
            {
                if ( pComponentHint == nullptr || pComponentHint[0] == '\0' )
                    return true;
                return object.findComponentByTypeName( hashed_string( pComponentHint ) ) != nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifiersPanel::MeshModifiersPanel()
        : IEditorPanel( false )
        , _curveState{}
        , _pCurveModifier{ nullptr }
        , _curveParamIndex{ 0 }
        , _descriptionID{}
        , _description{}
        , _bEditing{ false }
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
                    const bool bClicked = ImGui::MenuItem( listRegistration[item]->_pDisplayName );
                    drawRegistrationTooltip( *listRegistration[item] );
                    if ( bClicked == false )
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
        vector<string>                      listGroup;
        MeshModifierParamEdit::makeGroupOrder( listDef, listGroup );
        for ( const string& group : listGroup )
        {
            // 구역 없는 매개변수는 머리 없이 맨 위에 둔다(구역을 하나도 선언하지 않은 모디파이어는 예전 모양 그대로다)
            if ( group.empty() == false && ImGui::CollapsingHeader( group.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) == false )
                continue;
            for ( uint32 paramIndex = 0; paramIndex < modifier.getParamCount(); ++paramIndex )
            {
                if ( MeshModifierParamEdit::isInGroup( listDef[paramIndex], group ) )
                    drawParam( session, modifier, paramIndex );
            }
        }
    }

    void MeshModifiersPanel::drawParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::PushID( static_cast<int32>( paramIndex ) );
        switch ( def._kind )
        {
            case MeshModifierParamKind::Float:
            {
                float32      value  = modifier.getFloat( paramIndex );
                const string format = MeshModifiersPanelInternal::makeFormat( "%.3f", def._pUnit );
                if ( ImGui::DragFloat( def._pName, &value, MathUtil::max( ( def._max - def._min ) * 0.001f, 0.0001f ), def._min, def._max,
                                       format.c_str() ) )
                {
                    noteEdit( session );
                    modifier.setScalar( paramIndex, value );
                    modifier.onParamsChanged();
                }
                finishItem();
                MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
                break;
            }
            case MeshModifierParamKind::Int:
            {
                int32        value  = modifier.getInt( paramIndex );
                const string format = MeshModifiersPanelInternal::makeFormat( "%d", def._pUnit );
                if ( ImGui::DragInt( def._pName, &value, 0.1f, static_cast<int32>( def._min ), static_cast<int32>( def._max ), format.c_str() ) )
                {
                    noteEdit( session );
                    modifier.setScalar( paramIndex, static_cast<float32>( value ) );
                    modifier.onParamsChanged();
                }
                finishItem();
                MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
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
                MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
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
                MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
                break;
            }
            case MeshModifierParamKind::Vector3:
            {
                float3       value  = modifier.getVector3( paramIndex );
                const string format = MeshModifiersPanelInternal::makeFormat( "%.3f", def._pUnit );
                if ( ImGui::DragFloat3( def._pName, &value._x, 0.01f, def._min, def._max, format.c_str() ) )
                {
                    noteEdit( session );
                    modifier.setVector3( paramIndex, value );
                    modifier.onParamsChanged();
                }
                finishItem();
                MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
                break;
            }
            case MeshModifierParamKind::Curve:
                drawCurveParam( session, modifier, paramIndex );
                break;
            case MeshModifierParamKind::Reference:
                drawReferenceParam( session, modifier, paramIndex );
                break;
            case MeshModifierParamKind::AssetPath:
            {
                // 콘텐츠 브라우저 끌어놓기 · 지우기 · 찾아보기는 에디터 공용 슬롯이 맡는다. 바뀌는 순간이 한 번이라 바로 기록한다
                string text = modifier.getText( paramIndex );
                if ( EditorWidgets::drawAssetSlot( def._pName, text, def._pAssetFilterHint ) )
                {
                    session.recordUndo();
                    modifier.setText( paramIndex, text );
                    modifier.onParamsChanged();
                    session.markDirty();
                }
                MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip != nullptr ? def._pTooltip : def._pHint );
                break;
            }
            case MeshModifierParamKind::NumberList:
                drawNumberListParam( session, modifier, paramIndex );
                break;
        }
        ImGui::PopID();
    }

    void MeshModifiersPanel::drawCurveParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::TextUnformatted( def._pName );
        MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
        FloatCurve curve{};
        MeshModifierParamEdit::makeFloatCurve( modifier.getCurveKeys( paramIndex ), curve );
        if ( EditorCurveEditor::drawPreview( "##CurvePreview", curve, ImGui::GetFrameHeight() * 2.0f ) )
        {
            _curveState          = EditorCurveEditorState{};
            _curveState._working = curve;
            _pCurveModifier      = &modifier;
            _curveParamIndex     = paramIndex;
            ImGui::OpenPopup( "##CurveEditor" );
        }
        MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "CurveEditHint" ) );

        const float32 scale = EditorThemeUtil::getDpiScale();
        ImGui::SetNextWindowSize( ImVec2{ 560.0f * scale, 320.0f * scale }, ImGuiCond_Appearing );
        if ( ImGui::BeginPopup( "##CurveEditor", ImGuiWindowFlags_NoScrollbar ) == false )
            return;
        if ( _pCurveModifier == &modifier && _curveParamIndex == paramIndex )
        {
            ImGui::TextUnformatted( def._pName );
            // 끄는 중 · 칸 입력 중이 아니면 값에서 다시 뜬다(되돌리기 · 다른 곳의 편집이 편집기에 보인다)
            if ( _curveState._drag == EditorCurveDrag::None && ImGui::IsAnyItemActive() == false )
                _curveState._working = curve;
            if ( EditorCurveEditor::drawEditor( _curveState ) )
            {
                // 편집기는 작업 사본만 고친다 — 마칠 때 한 번 기록하고 넣으니 끌기 한 번이 되돌리기 한 줄이다
                vector<float2> listKey;
                MeshModifierParamEdit::makeCurveKeys( _curveState._working, listKey );
                session.recordUndo();
                modifier.setCurveKeys( paramIndex, listKey );
                modifier.onParamsChanged();
                session.markDirty();
            }
        }
        else
        {
            ImGui::CloseCurrentPopup(); // 다른 모디파이어를 고른 사이 열린 채 남은 팝업
        }
        ImGui::EndPopup();
    }

    void MeshModifiersPanel::drawNumberListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::TextUnformatted( def._pName );
        MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
        if ( def._pHint != nullptr )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(%s)", def._pHint );
        }

        vector<float32> listNumber   = modifier.getNumberList( paramIndex );
        const uint32    stride       = MathUtil::max<uint32>( def._stride, 1 );
        const bool      bHasColumns  = MeshModifierParamEdit::isColumnLayoutValid( def );
        const uint32    columnCount  = bHasColumns ? def._columnCount : stride;
        const size_t    rowCount     = MeshModifierParamEdit::getRowCount( listNumber, stride );
        bool            bChanged     = false;
        int32           upRow        = -1;
        int32           downRow      = -1;
        int32           duplicateAt  = -1;
        int32           removeAt     = -1;
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if ( rowCount > 0 && ImGui::BeginTable( "##Rows", static_cast<int32>( columnCount ) + 2, tableFlags ) )
        {
            ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed );
            for ( uint32 column = 0; column < columnCount; ++column )
            {
                const utf8* pColumnName = bHasColumns ? def._pColumn[column]._pName : MeshModifiersPanelInternal::getGenericColumnName( column );
                const bool  bWide       = bHasColumns && def._pColumn[column]._kind == MeshModifierListColumnKind::Vector3;
                ImGui::TableSetupColumn( pColumnName != nullptr ? pColumnName : "", ImGuiTableColumnFlags_WidthStretch, bWide ? 3.0f : 1.0f );
            }
            ImGui::TableSetupColumn( "##Actions", ImGuiTableColumnFlags_WidthFixed );
            ImGui::TableHeadersRow();

            for ( size_t row = 0; row < rowCount; ++row )
            {
                ImGui::TableNextRow();
                ImGui::PushID( static_cast<int32>( row ) );
                ImGui::TableNextColumn();
                ImGui::Text( "%d", static_cast<int32>( row ) );
                size_t element = row * stride;
                for ( uint32 column = 0; column < columnCount; ++column )
                {
                    ImGui::TableNextColumn();
                    ImGui::PushID( static_cast<int32>( column ) );
                    ImGui::SetNextItemWidth( -1.0f );
                    float32* pValue = &listNumber[element];
                    const MeshModifierListColumnKind kind = bHasColumns ? def._pColumn[column]._kind : MeshModifierListColumnKind::Float;
                    switch ( kind )
                    {
                        case MeshModifierListColumnKind::Float:
                        {
                            if ( ImGui::DragFloat( "##Value", pValue, 0.01f, def._min, def._max, "%.3f" ) )
                            {
                                noteEdit( session );
                                bChanged = true;
                            }
                            finishItem();
                            break;
                        }
                        case MeshModifierListColumnKind::Int:
                        {
                            int32 value = static_cast<int32>( MathUtil::round( *pValue ) );
                            if ( ImGui::DragInt( "##Value", &value, 0.1f, static_cast<int32>( def._min ), static_cast<int32>( def._max ) ) )
                            {
                                noteEdit( session );
                                *pValue  = static_cast<float32>( value );
                                bChanged = true;
                            }
                            finishItem();
                            break;
                        }
                        case MeshModifierListColumnKind::Combo:
                        {
                            const int32 itemCount = static_cast<int32>( MeshModifierParamEdit::getComboItemCount( def._pColumn[column]._pComboItems ) );
                            int32       item      = MathUtil::clamp( static_cast<int32>( MathUtil::round( *pValue ) ), 0, MathUtil::max( itemCount - 1, 0 ) );
                            if ( ImGui::Combo( "##Value", &item, def._pColumn[column]._pComboItems ) )
                            {
                                session.recordUndo();
                                *pValue  = static_cast<float32>( item );
                                bChanged = true;
                            }
                            break;
                        }
                        case MeshModifierListColumnKind::Vector3:
                        {
                            if ( ImGui::DragFloat3( "##Value", pValue, 0.01f, def._min, def._max, "%.3f" ) )
                            {
                                noteEdit( session );
                                bChanged = true;
                            }
                            finishItem();
                            break;
                        }
                    }
                    ImGui::PopID();
                    element += bHasColumns ? MeshModifierParamEdit::getColumnWidth( def._pColumn[column] ) : 1;
                }

                ImGui::TableNextColumn();
                if ( ImGui::SmallButton( "^" ) )
                    upRow = static_cast<int32>( row );
                MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "MoveRowUp" ) );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "v" ) )
                    downRow = static_cast<int32>( row );
                MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "MoveRowDown" ) );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "+" ) )
                    duplicateAt = static_cast<int32>( row );
                MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "DuplicateRow" ) );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "x" ) )
                    removeAt = static_cast<int32>( row );
                MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "RemoveRow" ) );
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if ( ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "AddItem" ) ) )
        {
            session.recordUndo();
            MeshModifierParamEdit::appendRow( listNumber, stride );
            bChanged = true;
        }

        // 표를 다 그린 뒤 행을 옮긴다(그리는 도중에 바꾸면 행 번호와 입력칸이 어긋난다). 범위는 방금 그린 행이라 맞다
        if ( upRow >= 0 || downRow >= 0 || duplicateAt >= 0 || removeAt >= 0 )
        {
            session.recordUndo();
            if ( upRow >= 0 )
                bChanged |= MeshModifierParamEdit::moveRowUp( listNumber, stride, static_cast<size_t>( upRow ) );
            else if ( downRow >= 0 )
                bChanged |= MeshModifierParamEdit::moveRowDown( listNumber, stride, static_cast<size_t>( downRow ) );
            else if ( duplicateAt >= 0 )
                bChanged |= MeshModifierParamEdit::duplicateRow( listNumber, stride, static_cast<size_t>( duplicateAt ) );
            else
                bChanged |= MeshModifierParamEdit::removeRow( listNumber, stride, static_cast<size_t>( removeAt ) );
        }
        if ( bChanged )
        {
            modifier.setNumberList( paramIndex, listNumber );
            modifier.onParamsChanged();
            session.markDirty();
        }
    }

    void MeshModifiersPanel::drawReferenceParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def     = modifier.getParamDefs()[paramIndex];
        const string&               current = modifier.getText( paramIndex );
        string                      picked  = current;
        bool                        bPicked = false;
        const utf8* pPreview = current.empty() ? SW_LOCKEY( "EdMeshModifiers", "ReferenceNone" ) : current.c_str();
        if ( ImGui::BeginCombo( def._pName, pPreview ) )
        {
            if ( ImGui::Selectable( SW_LOCKEY( "EdMeshModifiers", "ReferenceNone" ), current.empty() ) )
            {
                picked  = string();
                bPicked = true;
            }
            GameObjectManager*  pManager = getActiveObjectManager();
            vector<GameObject*> listObject;
            if ( pManager != nullptr )
                pManager->getAllGameObjects( listObject );
            // 걸러 볼 컴포넌트가 있으면 그것을 가진 오브젝트만, 하나도 없으면 알리고 전체를 보인다
            bool bAnyMatch = false;
            for ( const GameObject* pObject : listObject )
            {
                if ( pObject != nullptr && MeshModifiersPanelInternal::matchesComponentHint( *pObject, def._pRequiredComponentHint ) )
                {
                    bAnyMatch = true;
                    break;
                }
            }
            if ( bAnyMatch == false && def._pRequiredComponentHint != nullptr )
                ImGui::TextDisabled( "%s %s", SW_LOCKEY( "EdMeshModifiers", "ReferenceNoMatch" ), def._pRequiredComponentHint );
            for ( const GameObject* pObject : listObject )
            {
                if ( pObject == nullptr )
                    continue;
                if ( bAnyMatch && MeshModifiersPanelInternal::matchesComponentHint( *pObject, def._pRequiredComponentHint ) == false )
                    continue;
                const utf8* pName = pObject->getName().c_str();
                if ( pName == nullptr || pName[0] == '\0' )
                    continue; // 참조는 이름으로 찾으므로 이름 없는 오브젝트는 고를 수 없다
                ImGui::PushID( pObject );
                if ( ImGui::Selectable( pName, string_view( current ) == string_view( pName ) ) )
                {
                    picked  = string( pName );
                    bPicked = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip != nullptr ? def._pTooltip : def._pHint );

        if ( ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "UseSelectionForParam" ) ) )
        {
            EditorSelection* pSelection = getService<EditorSelection>();
            GameObject*      pObject    = pSelection != nullptr ? pSelection->getPrimaryObject() : nullptr;
            if ( pObject == nullptr )
                SW_LOG_WARNING( "Select an object in the scene first" );
            else if ( MeshModifiersPanelInternal::matchesComponentHint( *pObject, def._pRequiredComponentHint ) == false )
                SW_LOG_WARNING( "Object %# has no %# component", pObject->getName().c_str(), def._pRequiredComponentHint );
            else
            {
                picked  = string( pObject->getName().c_str() );
                bPicked = true;
            }
        }
        if ( def._pRequiredComponentHint != nullptr )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(%s)", def._pRequiredComponentHint );
        }

        if ( bPicked && string_view( picked ) != string_view( current ) )
        {
            session.recordUndo();
            modifier.setText( paramIndex, picked );
            modifier.onParamsChanged();
            session.markDirty();
        }
    }

    void MeshModifiersPanel::drawRegistrationTooltip( const MeshModifierRegistration& registration )
    {
        if ( ImGui::IsItemHovered() == false )
            return;
        if ( string_view( _descriptionID ) != string_view( registration._pID ) )
        {
            // 설명은 인스턴스의 가상 함수라 하나 만들어 묻는다 — 마우스가 새 항목에 올 때 한 번뿐이다
            _descriptionID = string( registration._pID );
            const unique_ptr<MeshModifier> pModifier = registration._pCreateFn();
            _description = pModifier != nullptr ? string( pModifier->getDescription() ) : string();
        }
        if ( _description.empty() == false )
            ImGui::SetTooltip( "%s", _description.c_str() );
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
        // 고른 점 · 대칭은 세션 상태다 — 뷰포트에서 점을 고르면 이 칸이 따라가고, 대칭 설정은 뷰포트 끌기에도 걸린다
        bool bMirror = session.isMirrorEditing();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "Mirror" ), &bMirror ) )
            session.setMirrorEditing( bMirror );
        if ( session.isMirrorEditing() )
        {
            ImGui::SameLine();
            int32 axis = static_cast<int32>( session.getMirrorAxis() );
            ImGui::SetNextItemWidth( 60.0f );
            if ( ImGui::Combo( "##MirrorAxis", &axis, MeshModifiersPanelInternal::kAxisNames ) )
                session.setMirrorAxis( static_cast<uint32>( MathUtil::clamp( axis, 0, 2 ) ) );
        }

        const int32 count = lattice.getControlPointCount();
        if ( count <= 0 )
            return;
        // 고른 점이 없으면(-1) 칸은 0 을 보이고, 칸을 움직이면 그 점을 고른다
        int32 selected = MathUtil::clamp( session.getSelectedControlPoint(), 0, count - 1 );
        if ( ImGui::SliderInt( SW_LOCKEY( "EdMeshModifiers", "PointIndex" ), &selected, 0, count - 1 ) )
        {
            session.setSelectedControlPoint( selected );
            session.markDirty();
        }
        float3 position = lattice.getControlPoint( selected );
        if ( ImGui::DragFloat3( SW_LOCKEY( "EdMeshModifiers", "PointPosition" ), &position._x, 0.005f ) )
        {
            noteEdit( session );
            session.setSelectedControlPoint( selected );
            const float3        delta = position - lattice.getControlPoint( selected );
            const vector<int32> listIndex{ selected };
            lattice.moveControlPoints( listIndex, delta, session.isMirrorEditing(), session.getMirrorAxis() );
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
