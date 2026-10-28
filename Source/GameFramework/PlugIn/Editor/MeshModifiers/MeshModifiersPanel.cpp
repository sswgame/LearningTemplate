/**
 * @file MeshModifiersPanel.cpp
 * @brief Mesh Modifiers 패널을 구현합니다. 매개변수 입력칸은 선언 종류(Float · Int · Bool · Axis · Vector3 · Curve · Reference · AssetPath · NumberList · CurveList · ReferenceList)로 고릅니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifiersPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/FloatCurve.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/MeshModifierSession.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/DeformableModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/LatticeModifier.h"
#include "GameFramework/PlugIn/Editor/MeshModifiers/Modifiers/WorldWarpModifier.h"
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
                utf8         arrBuffer[constant::kMaxPathSize] = {};
                const size_t copyCount                         = MathUtil::min( value.size(), static_cast<size_t>( constant::kMaxPathSize - 1 ) );
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

            /** @brief 자동화가 켜졌을 때만 `<접두>.<꼬리>` 키를 만들어 직전 위젯을 기록합니다(키 글 만드는 비용을 평소에는 내지 않는다). */
            static void noteMark( const utf8* pPrefix, string_view tail )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                string key( pPrefix );
                key += ".";
                key += tail;
                EditorSelfTestMarks::note( key.c_str() );
            }

            static void noteMark( const utf8* pPrefix, size_t index )
            {
                if ( EditorSelfTestMarks::isEnabled() )
                    noteMark( pPrefix, string_view( to_string( static_cast<uint32>( index ) ) ) );
            }

            /** @brief 직전 항목에 마우스가 있으면 툴팁을 보입니다. */
            static void drawItemTooltip( const utf8* pTooltip )
            {
                const bool bTooltipVisible = pTooltip != nullptr && pTooltip[0] != '\0' && ImGui::IsItemHovered();
                if ( bTooltipVisible )
                    ImGui::SetTooltip( "%s", pTooltip );
            }

            /** @brief 목록 항목 한 줄의 위 · 아래 · 복제 · 삭제 단추입니다. 눌린 것은 @p inoutAction 에 담고 목록은 그린 뒤에 고친다(그리는 도중에 바꾸면 항목 번호가 어긋난다). */
            static void drawItemButtons( size_t item, MeshModifierListAction& inoutAction )
            {
                if ( ImGui::SmallButton( "^" ) )
                    inoutAction = MeshModifierListAction{ MeshModifierListActionKind::MoveUp, item };
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "MoveRowUp" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "v" ) );
                if ( ImGui::SmallButton( "v" ) )
                    inoutAction = MeshModifierListAction{ MeshModifierListActionKind::MoveDown, item };
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "MoveRowDown" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "+" ) );
                if ( ImGui::SmallButton( "+" ) )
                    inoutAction = MeshModifierListAction{ MeshModifierListActionKind::Duplicate, item };
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "DuplicateRow" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "x" ) );
                if ( ImGui::SmallButton( "x" ) )
                    inoutAction = MeshModifierListAction{ MeshModifierListActionKind::Remove, item };
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "RemoveRow" ) );
            }

            /**
             * @brief 씬 오브젝트 고르기 콤보와 "선택 사용" 단추를 그립니다(참조 하나 · 참조 목록 항목이 함께 쓴다).
             * @return 이름을 새로 골랐으면 true 이고 @p outPicked 에 이름이 들어 있다(빈 글 = 비우기). 같은 이름을 다시 골라도 true 일 수 있다.
             */
            static bool drawReferencePicker( const MeshModifierParamDef& def, const utf8* pLabel, const string& current, string& outPicked, const utf8* pMarkKey )
            {
                bool        bPicked  = false;
                const utf8* pPreview = current.empty() ? SW_LOCKEY( "EdMeshModifiers", "ReferenceNone" ) : current.c_str();
                if ( ImGui::BeginCombo( pLabel, pPreview ) )
                {
                    if ( ImGui::Selectable( SW_LOCKEY( "EdMeshModifiers", "ReferenceNone" ), current.empty() ) )
                    {
                        outPicked = string();
                        bPicked   = true;
                    }
                    GameObjectManager*  pManager = getActiveObjectManager();
                    vector<GameObject*> listObject;
                    if ( pManager != nullptr )
                        pManager->getAllGameObjects( listObject );
                    // 걸러 볼 컴포넌트가 있으면 그것을 가진 오브젝트만, 하나도 없으면 알리고 전체를 보인다
                    bool bAnyMatch = false;
                    for ( const GameObject* pObject : listObject )
                    {
                        if ( pObject != nullptr && MeshModifierParamEdit::matchesComponentHint( *pObject, def._pRequiredComponentHint ) )
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
                        if ( bAnyMatch && MeshModifierParamEdit::matchesComponentHint( *pObject, def._pRequiredComponentHint ) == false )
                            continue;
                        const utf8* pName = pObject->getName().c_str();
                        if ( pName == nullptr || pName[0] == '\0' )
                            continue; // 참조는 이름으로 찾으므로 이름 없는 오브젝트는 고를 수 없다
                        ImGui::PushID( pObject );
                        if ( ImGui::Selectable( pName, string_view( current ) == string_view( pName ) ) )
                        {
                            outPicked = string( pName );
                            bPicked   = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                drawItemTooltip( def._pTooltip != nullptr ? def._pTooltip : def._pHint );

                const bool bUseSelection = ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "UseSelectionForParam" ) );
                noteMark( pMarkKey, "useSelection" );
                if ( bUseSelection )
                {
                    // 모델링 도구는 레벨의 오브젝트를 고친다(언리얼 Modeling Mode 는 레벨 선택) — 메인 씬의 선택이다.
                    GameObject* pObject = editor::getMainSceneSelection().getPrimaryObject();
                    if ( pObject == nullptr )
                        SW_LOG_WARNING( "Select an object in the scene first" );
                    else if ( MeshModifierParamEdit::matchesComponentHint( *pObject, def._pRequiredComponentHint ) == false )
                        SW_LOG_WARNING( "Object %# has no %# component", pObject->getName().c_str(), def._pRequiredComponentHint );
                    else
                    {
                        outPicked = string( pObject->getName().c_str() );
                        bPicked   = true;
                    }
                }
                if ( def._pRequiredComponentHint != nullptr )
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled( "(%s)", def._pRequiredComponentHint );
                }
                return bPicked;
            }

            /** @brief 숫자 목록 표에서 한 행에 눌린 행 조작(위로 · 아래로 · 복제 · 삭제)입니다. 없으면 -1 입니다. */
            struct NumberRowAction
            {
                int32 _upRow{ -1 };
                int32 _downRow{ -1 };
                int32 _duplicateAt{ -1 };
                int32 _removeAt{ -1 };

                bool isRequested() const { return _upRow >= 0 || _downRow >= 0 || _duplicateAt >= 0 || _removeAt >= 0; }
            };

            /** @brief 행 끝의 위 · 아래 · 복제 · 삭제 단추를 그리고 눌린 것을 @p inoutAction 에 적습니다. */
            static void drawNumberRowButtons( size_t row, NumberRowAction& inoutAction )
            {
                if ( ImGui::SmallButton( "^" ) )
                    inoutAction._upRow = static_cast<int32>( row );
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "MoveRowUp" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "v" ) );
                if ( ImGui::SmallButton( "v" ) )
                    inoutAction._downRow = static_cast<int32>( row );
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "MoveRowDown" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "+" ) );
                if ( ImGui::SmallButton( "+" ) )
                    inoutAction._duplicateAt = static_cast<int32>( row );
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "DuplicateRow" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "x" ) );
                if ( ImGui::SmallButton( "x" ) )
                    inoutAction._removeAt = static_cast<int32>( row );
                drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "RemoveRow" ) );
            }

            /** @brief 눌린 행 조작 하나를 목록에 적용합니다. 표를 다 그린 뒤 부른다(그리는 도중에 바꾸면 행 번호와 입력칸이 어긋난다). 범위는 방금 그린 행이라 맞다. 바뀌었으면 true 입니다. */
            [[nodiscard]] static bool applyNumberRowAction( const NumberRowAction& action, vector<float32>& inoutListNumber, uint32 stride )
            {
                if ( action._upRow >= 0 )
                    return MeshModifierParamEdit::moveRowUp( inoutListNumber, stride, static_cast<size_t>( action._upRow ) );
                if ( action._downRow >= 0 )
                    return MeshModifierParamEdit::moveRowDown( inoutListNumber, stride, static_cast<size_t>( action._downRow ) );
                if ( action._duplicateAt >= 0 )
                    return MeshModifierParamEdit::duplicateRow( inoutListNumber, stride, static_cast<size_t>( action._duplicateAt ) );
                return MeshModifierParamEdit::removeRow( inoutListNumber, stride, static_cast<size_t>( action._removeAt ) );
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
        , _curveItemIndex{ -1 }
        , _descriptionID{}
        , _description{}
        , _pNestedRoot{ nullptr }
        , _listNestedPath{}
        , _nestedSelectedIndex{ -1 }
        , _warpExportPath{}
        , _bEditing{ false }
    {
    }

    void MeshModifiersPanel::drawContent()
    {
        MeshModifierSession* pSession = findSubsystem<MeshModifierSession>();
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
        session.advancePreviewTime( ImGui::GetIO().DeltaTime );
        session.refreshPreview();
    }

    bool MeshModifiersPanel::saveDocument()
    {
        MeshModifierSession* pSession = findSubsystem<MeshModifierSession>();
        return pSession != nullptr && pSession->saveSidecar(); // 실패는 saveSidecar 가 알린다
    }

    bool MeshModifiersPanel::canSaveDocument() const
    {
        const MeshModifierSession* pSession = findSubsystem<MeshModifierSession>();
        return pSession != nullptr && pSession->isStackUnsaved();
    }

    void MeshModifiersPanel::syncDocumentDirty()
    {
        // 편집 기록은 세션 안에 있어 패널이 편집마다 알리지 않는다 — 그리기마다 세션의 스택을 기준으로 다시 맞춘다(`CharacterEditorPanel` 과 같은 모양).
        if ( canSaveDocument() )
            markDocumentDirty();
        else
            clearDocumentDirty();
    }

    void MeshModifiersPanel::revertDocument()
    {
        MeshModifierSession* pSession = findSubsystem<MeshModifierSession>();
        if ( pSession != nullptr )
            pSession->revertStackToSaved();
    }

    void MeshModifiersPanel::drawTargetRow( MeshModifierSession& session )
    {
        // 저장하지 않은 스택이 있으면 대상을 바꾸지 않는다 — 바꾸면 세션이 스택을 비워 편집이 조용히 사라진다.
        const bool bUnsaved = session.isStackUnsaved();
        ImGui::BeginDisabled( bUnsaved );
        const bool bUseSelection = ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "UseSelection" ) );
        ImGui::EndDisabled();
        if ( bUnsaved && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            ImGui::SetTooltip( "Save or reload the modifier stack before picking another mesh" );
        EditorSelfTestMarks::note( "meshmodifiers.useSelection" );
        if ( bUseSelection )
        {
            GameObject* pObject = editor::getMainSceneSelection().getPrimaryObject(); // 메인 씬의 선택(위 `UseSelectionForParam` 과 같다)
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
        const int32        added = drawAddMenu( session, stack, "meshmodifiers" );
        if ( added >= 0 )
            session.setSelectedIndex( added );
        session.setSelectedIndex( drawStackRows( session, stack, session.getSelectedIndex(), "meshmodifiers.row", nullptr ) );
    }

    int32 MeshModifiersPanel::drawStackRows( MeshModifierSession& session, MeshModifierStack& stack, int32 selectedIndex, const utf8* pMarkPrefix, int32* pOutEnterIndex )
    {
        int32 selected    = selectedIndex;
        int32 removeIndex = -1;
        int32 upIndex     = -1;
        int32 downIndex   = -1;
        // 묶음(Deformable) 줄은 ">" 단추로 그 하위 스택에 들어간다 - 단추 칸만큼 이름 칸을 줄인다
        const float32 buttonWidth = pOutEnterIndex != nullptr ? 115.0f : 90.0f;
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
            if ( ImGui::Selectable( pModifier->getName().c_str(), selected == index, ImGuiSelectableFlags_None, ImVec2{ ImGui::GetContentRegionAvail().x - buttonWidth, 0.0f } ) )
                selected = index;
            MeshModifiersPanelInternal::noteMark( pMarkPrefix, static_cast<size_t>( index ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "^" ) );
            if ( ImGui::SmallButton( "^" ) )
                upIndex = index;
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "v" ) );
            if ( ImGui::SmallButton( "v" ) )
                downIndex = index;
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "x" ) );
            if ( ImGui::SmallButton( "x" ) )
                removeIndex = index;
            if ( pOutEnterIndex != nullptr && DeformableModifier::findFrom( *pModifier ) != nullptr )
            {
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( ">" ) );
                if ( ImGui::SmallButton( ">" ) )
                    *pOutEnterIndex = index;
                MeshModifiersPanelInternal::noteMark( "meshmodifiers.enter", static_cast<size_t>( index ) );
                MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "EnterNestedStack" ) );
            }
            ImGui::PopID();
        }

        // 목록을 다 그린 뒤 바꾼다(그리는 도중에 지우면 포인터가 무효가 된다)
        if ( upIndex >= 0 )
        {
            session.recordUndo();
            if ( stack.moveUp( upIndex ) )
                selected = upIndex - 1;
            session.markDirty();
        }
        if ( downIndex >= 0 )
        {
            session.recordUndo();
            if ( stack.moveDown( downIndex ) )
                selected = downIndex + 1;
            session.markDirty();
        }
        if ( removeIndex >= 0 )
        {
            session.recordUndo();
            (void)stack.removeModifier( removeIndex ); // 방금 그린 번호라 범위 안이다
            selected = MathUtil::min( selected, stack.getModifierCount() - 1 );
            session.markDirty();
        }
        return selected;
    }

    void MeshModifiersPanel::drawNestedStack( MeshModifierSession& session, DeformableModifier& root )
    {
        ImGui::SeparatorText( SW_LOCKEY( "EdMeshModifiers", "NestedStack" ) );
        if ( _pNestedRoot != &root )
        {
            // 다른 묶음을 골랐다 - 하위 스택의 고른 자리를 처음으로 되돌린다(포인터는 비교만 한다)
            _pNestedRoot = &root;
            _listNestedPath.clear();
            _nestedSelectedIndex = -1;
        }
        // 보여 줄 스택 = 경로가 가리키는 묶음의 하위 스택(빈 경로면 고른 묶음 자신의 하위 스택). 되돌리기로 경로가 무효가 되면 처음으로
        MeshModifierStack* pShown = &root.getStack();
        if ( _listNestedPath.empty() == false )
        {
            MeshModifier*       pOwner = DeformableModifier::findByPath( root.getStack(), _listNestedPath );
            DeformableModifier* pGroup = pOwner != nullptr ? DeformableModifier::findFrom( *pOwner ) : nullptr;
            if ( pGroup != nullptr )
                pShown = &pGroup->getStack();
            else
            {
                _listNestedPath.clear();
                _nestedSelectedIndex = -1;
            }
        }

        ImGui::PushID( "##NestedStack" );
        ImGui::PushID( static_cast<int32>( _listNestedPath.size() ) );
        if ( _listNestedPath.empty() == false )
        {
            const bool bUp = ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "NestedUp" ) );
            EditorSelfTestMarks::note( "meshmodifiers.nested.up" );
            if ( bUp )
            {
                _nestedSelectedIndex = _listNestedPath.back();
                _listNestedPath.pop_back();
            }
            ImGui::SameLine();
        }
        ImGui::TextDisabled( "%s %u", SW_LOCKEY( "EdMeshModifiers", "NestedDepth" ), static_cast<uint32>( _listNestedPath.size() + 1 ) );
        const int32 added = drawAddMenu( session, *pShown, "meshmodifiers.nested" );
        if ( added >= 0 )
            _nestedSelectedIndex = added;
        int32 enterIndex     = -1;
        _nestedSelectedIndex = drawStackRows( session, *pShown, _nestedSelectedIndex, "meshmodifiers.nested.row", &enterIndex );
        if ( pShown->getModifierCount() == 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdMeshModifiers", "NestedEmpty" ) );
        drawWarpExport( *pShown );
        if ( enterIndex >= 0 )
        {
            _listNestedPath.push_back( enterIndex );
            _nestedSelectedIndex = -1;
        }
        else
        {
            MeshModifier* pSelected = pShown->findModifier( _nestedSelectedIndex );
            if ( pSelected != nullptr )
            {
                // 하위 모디파이어의 기즈모 · 매개변수 - 묶음의 기즈모 공간 기준이다. 편집은 바깥 스택 문서(extra.stack)째 되돌리기에 들어간다
                ImGui::SeparatorText( pSelected->getName().c_str() );
                ImGui::PushID( pSelected );
                drawGizmo( session, *pSelected );
                drawParams( session, *pSelected );
                LatticeModifier* pLattice = LatticeModifier::findFrom( *pSelected );
                if ( pLattice != nullptr )
                    drawLatticePoints( session, *pLattice );
                ImGui::PopID();
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    void MeshModifiersPanel::drawWarpExport( const MeshModifierStack& shown )
    {
        // 보이는 하위 스택을 워프 정의 파일로 쓴다 — World Warp 모디파이어가 그 파일을 가리키면 여러 메시가 한 워프를 함께 받는다.
        // 경로는 사용자가 정한다(확장자가 `.mfwarp` 가 아니면 단추를 끈다 — 에셋 슬롯 필터가 그 확장자만 고른다).
        (void)MeshModifiersPanelInternal::drawTextInput( SW_LOCKEY( "EdMeshModifiers", "WarpExportPath" ), _warpExportPath, _warpExportPath );
        EditorSelfTestMarks::note( "meshmodifiers.nested.warppath" );
        const string_view extension     = WorldWarpModifier::kFileExtension;
        const bool        bWarpFilePath = _warpExportPath.size() > extension.size() &&
                                   string_view( _warpExportPath ).substr( _warpExportPath.size() - extension.size() ) == extension;
        ImGui::BeginDisabled( bWarpFilePath == false || shown.getModifierCount() == 0 );
        const bool bExport = ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "ExportWarp" ) );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "meshmodifiers.nested.exportwarp" );
        if ( bExport == false )
            return;
        if ( WorldWarpModifier::writeWarpFile( shown, _warpExportPath ) )
            SW_LOG_INFO( "Exported %# modifiers as the world warp '%#'", shown.getModifierCount(), _warpExportPath.c_str() );
        else
            SW_LOG_ERROR( "Could not write the world warp '%#'", _warpExportPath.c_str() );
    }

    int32 MeshModifiersPanel::drawAddMenu( MeshModifierSession& session, MeshModifierStack& stack, const utf8* pMarkPrefix )
    {
        int32      addedIndex = -1;
        const bool bOpenMenu  = ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "AddModifier" ) );
        MeshModifiersPanelInternal::noteMark( pMarkPrefix, "add" );
        if ( bOpenMenu )
            EditorInputGUI::openPopup( "##AddModifier" );
        if ( ImGui::BeginPopup( "##AddModifier" ) == false )
            return addedIndex;
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
            const bool bCategoryOpen = ImGui::BeginMenu( listRegistration[index]->_pCategory );
            MeshModifiersPanelInternal::noteMark( pMarkPrefix, string( "category." ) + string( category ) ); // BeginMenu 는 마지막 항목을 메뉴 머리로 되돌려 둔다
            if ( bCategoryOpen )
            {
                for ( size_t item = index; item < end; ++item )
                {
                    const bool bClicked = ImGui::MenuItem( listRegistration[item]->_pDisplayName );
                    MeshModifiersPanelInternal::noteMark( pMarkPrefix, string( "item." ) + listRegistration[item]->_pID );
                    drawRegistrationTooltip( *listRegistration[item] );
                    if ( bClicked == false )
                        continue;
                    session.recordUndo();
                    const int32   newIndex = stack.addModifier( listRegistration[item]->_pID );
                    MeshModifier* pAdded   = stack.findModifier( newIndex );
                    if ( pAdded != nullptr )
                        pAdded->fitToBounds( session.getSourceBounds() ); // 하위 스택이면 묶음 공간과 다를 수 있다 - 맞추기 단추로 다시 맞춘다
                    addedIndex = newIndex;
                    session.markDirty();
                }
                ImGui::EndMenu();
            }
            index = end;
        }
        ImGui::EndPopup();
        return addedIndex;
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
        DeformableModifier* pDeformable = DeformableModifier::findFrom( *pModifier );
        if ( pDeformable != nullptr )
            drawNestedStack( session, *pDeformable );
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
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdMeshModifiers", "ResetParams" ) ) );
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

    // 길이를 나누지 않는다: 매개변수 종류마다 짧은 경우(입력칸 하나 + 값 쓰기 + 툴팁)를 늘어놓은 switch 디스패처다. 긴 경우(곡선 · 참조 · 숫자 목록)는 이미 따로 뺐고,
    // 남은 경우를 더 가르면 같은 모양의 함수가 종류 수만큼 생겨 새 종류를 더할 때 고칠 곳이 둘이 된다.
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
            {
                drawCurveParam( session, modifier, paramIndex );
                break;
            }
            case MeshModifierParamKind::Reference:
            {
                drawReferenceParam( session, modifier, paramIndex );
                break;
            }
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
            {
                drawNumberListParam( session, modifier, paramIndex );
                break;
            }
            case MeshModifierParamKind::CurveList:
            {
                drawCurveListParam( session, modifier, paramIndex );
                break;
            }
            case MeshModifierParamKind::ReferenceList:
            {
                drawReferenceListParam( session, modifier, paramIndex );
                break;
            }
        }
        MeshModifiersPanelInternal::noteMark( "meshmodifiers.param", string_view( def._pName ) ); // 스칼라 · 벡터 칸은 입력칸 자신이다
        ImGui::PopID();
    }

    void MeshModifiersPanel::drawCurveParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::TextUnformatted( def._pName );
        MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
        drawCurveField( session, modifier, paramIndex, -1, modifier.getParamValue( paramIndex )._curve );
    }

    void MeshModifiersPanel::drawCurveField( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex, int32 itemIndex, const FloatCurve& curve )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        // 매개변수의 곡선은 편집기와 같은 FloatCurve(보간 · 접선 포함)라 그대로 넘기고 그대로 받는다
        const bool bOpen = EditorCurveEditor::drawPreview( "##CurvePreview", curve, ImGui::GetFrameHeight() * 2.0f );
        if ( itemIndex < 0 )
            EditorSelfTestMarks::note( "meshmodifiers.curve" );
        else
            MeshModifiersPanelInternal::noteMark( "meshmodifiers.curve", static_cast<size_t>( itemIndex ) );
        if ( bOpen )
        {
            _curveState          = EditorCurveEditorState{};
            _curveState._working = curve;
            _pCurveModifier      = &modifier;
            _curveParamIndex     = paramIndex;
            _curveItemIndex      = itemIndex;
            EditorInputGUI::openPopup( "##CurveEditor" );
        }
        MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "CurveEditHint" ) );

        EditorChrome::setNextWindowExtent( EditorTextExtent::ofText( 75.0f, 17.0f ), false );
        if ( ImGui::BeginPopup( "##CurveEditor", ImGuiWindowFlags_NoScrollbar ) == false )
            return;
        const bool bSameParam = _pCurveModifier == &modifier && _curveParamIndex == paramIndex;
        if ( bSameParam && _curveItemIndex == itemIndex )
        {
            ImGui::TextUnformatted( def._pName );
            // 끄는 중 · 칸 입력 중이 아니면 값에서 다시 뜬다(되돌리기 · 다른 곳의 편집이 편집기에 보인다)
            if ( _curveState._drag == EditorCurveDrag::None && ImGui::IsAnyItemActive() == false )
                _curveState._working = curve;
            if ( EditorCurveEditor::drawEditor( _curveState ) )
            {
                // 편집기는 작업 사본만 고친다 - 마칠 때 한 번 기록하고 넣으니 끌기 한 번이 되돌리기 한 줄이다
                MeshModifierParamValue value = modifier.getParamValue( paramIndex );
                if ( itemIndex < 0 )
                    value._curve = _curveState._working;
                else if ( static_cast<size_t>( itemIndex ) < value._listCurve.size() )
                    value._listCurve[static_cast<size_t>( itemIndex )] = _curveState._working;
                session.recordUndo();
                modifier.setParamValue( paramIndex, value );
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

    void MeshModifiersPanel::drawCurveListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::TextUnformatted( def._pName );
        MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
        if ( def._pHint != nullptr )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(%s)", def._pHint );
        }

        MeshModifierParamValue value = modifier.getParamValue( paramIndex );
        const size_t           limit = def.getListLimit();
        MeshModifierListAction action{};
        for ( size_t item = 0; item < value._listCurve.size(); ++item )
        {
            ImGui::PushID( static_cast<int32>( item ) );
            ImGui::Text( "#%d", static_cast<int32>( item ) );
            ImGui::SameLine();
            MeshModifiersPanelInternal::drawItemButtons( item, action );
            drawCurveField( session, modifier, paramIndex, static_cast<int32>( item ), value._listCurve[item] );
            ImGui::PopID();
        }
        bool bAddItem = false;
        if ( value._listCurve.size() < limit )
        {
            bAddItem = ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "AddItem" ) );
            EditorSelfTestMarks::note( "meshmodifiers.curveList.add" );
        }
        if ( bAddItem )
        {
            // 새 항목은 선형 0 → 1 곡선으로 시작한다(모양 곡선의 기본과 같다)
            FloatCurve curve{};
            (void)curve.addKey( 0.0f, 0.0f, CurveInterpolation::Linear );
            (void)curve.addKey( 1.0f, 1.0f, CurveInterpolation::Linear );
            for ( FloatCurveKey& key : curve._listKey )
            {
                key._bAutoTangent = false;
            }
            value._listCurve.push_back( curve );
            action._kind = MeshModifierListActionKind::Added;
        }
        if ( MeshModifierParamEdit::applyListAction( action, value._listCurve, limit ) )
        {
            _pCurveModifier = nullptr; // 항목 번호가 바뀌었으니 열린 곡선 팝업은 닫는다
            session.recordUndo();
            modifier.setParamValue( paramIndex, value );
            modifier.onParamsChanged();
            session.markDirty();
        }
    }

    bool MeshModifiersPanel::drawNumberCell( MeshModifierSession& session, const MeshModifierParamDef& def, bool bHasColumns, uint32 column, float32* pValue )
    {
        bool                             bChanged = false;
        const MeshModifierListColumnKind kind     = bHasColumns ? def._pColumn[column]._kind : MeshModifierListColumnKind::Float;
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
        return bChanged;
    }

    void MeshModifiersPanel::drawNumberListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        using Internal                  = MeshModifiersPanelInternal;
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::TextUnformatted( def._pName );
        Internal::drawItemTooltip( def._pTooltip );
        if ( def._pHint != nullptr )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(%s)", def._pHint );
        }

        vector<float32>           listNumber  = modifier.getNumberList( paramIndex );
        const uint32              stride      = MathUtil::max<uint32>( def._stride, 1 );
        const bool                bHasColumns = MeshModifierParamEdit::isColumnLayoutValid( def );
        const uint32              columnCount = bHasColumns ? def._columnCount : stride;
        const size_t              rowCount    = MeshModifierParamEdit::getRowCount( listNumber, stride );
        bool                      bChanged    = false;
        Internal::NumberRowAction rowAction{};
        const ImGuiTableFlags     tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if ( rowCount > 0 && ImGui::BeginTable( "##Rows", static_cast<int32>( columnCount ) + 2, tableFlags ) )
        {
            ImGui::TableSetupColumn( "#", ImGuiTableColumnFlags_WidthFixed );
            for ( uint32 column = 0; column < columnCount; ++column )
            {
                const utf8* pColumnName = bHasColumns ? def._pColumn[column]._pName : Internal::getGenericColumnName( column );
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
                    bChanged |= drawNumberCell( session, def, bHasColumns, column, &listNumber[element] );
                    ImGui::PopID();
                    element += bHasColumns ? MeshModifierParamEdit::getColumnWidth( def._pColumn[column] ) : 1;
                }

                ImGui::TableNextColumn();
                Internal::drawNumberRowButtons( row, rowAction );
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
        if ( rowAction.isRequested() )
        {
            session.recordUndo();
            bChanged |= Internal::applyNumberRowAction( rowAction, listNumber, stride );
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
        if ( MeshModifiersPanelInternal::drawReferencePicker( def, def._pName, current, picked, "meshmodifiers.reference" ) && string_view( picked ) != string_view( current ) )
        {
            session.recordUndo();
            modifier.setText( paramIndex, picked );
            modifier.onParamsChanged();
            session.markDirty();
        }
    }

    void MeshModifiersPanel::drawReferenceListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex )
    {
        const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
        ImGui::TextUnformatted( def._pName );
        MeshModifiersPanelInternal::drawItemTooltip( def._pTooltip );
        if ( def._pHint != nullptr )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "(%s)", def._pHint );
        }

        MeshModifierParamValue value   = modifier.getParamValue( paramIndex );
        const size_t           limit   = def.getListLimit();
        bool                   bChange = false;
        MeshModifierListAction action{};
        for ( size_t item = 0; item < value._listText.size(); ++item )
        {
            ImGui::PushID( static_cast<int32>( item ) );
            ImGui::Text( "#%d", static_cast<int32>( item ) );
            ImGui::SameLine();
            MeshModifiersPanelInternal::drawItemButtons( item, action );
            const string current = value._listText[item];
            string       picked  = current;
            const string markKey = string( "meshmodifiers.referenceList." ) + to_string( static_cast<uint32>( item ) );
            if ( MeshModifiersPanelInternal::drawReferencePicker( def, "##Item", current, picked, markKey.c_str() ) && string_view( picked ) != string_view( current ) )
            {
                value._listText[item] = picked;
                bChange               = true;
            }
            ImGui::PopID();
        }
        bool bAddItem = false;
        if ( value._listText.size() < limit )
        {
            bAddItem = ImGui::SmallButton( SW_LOCKEY( "EdMeshModifiers", "AddItem" ) );
            EditorSelfTestMarks::note( "meshmodifiers.referenceList.add" );
        }
        if ( bAddItem )
        {
            value._listText.push_back( string() ); // 빈 항목 - 고를 때까지 건너뛴다
            action._kind = MeshModifierListActionKind::Added;
        }
        if ( MeshModifierParamEdit::applyListAction( action, value._listText, limit ) || bChange )
        {
            session.recordUndo();
            modifier.setParamValue( paramIndex, value );
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
            _descriptionID                           = string( registration._pID );
            const unique_ptr<MeshModifier> pModifier = registration._pCreateFn();
            _description                             = pModifier != nullptr ? string( pModifier->getDescription() ) : string();
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
        // 고른 점 · 대칭은 세션 상태다 — 뷰포트에서 점을 고르면 이 칸이 따라가고, 대칭 설정은 뷰포트 끌기에도 걸린다
        bool bMirror = session.isMirrorEditing();
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdMeshModifiers", "Mirror" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "Mirror" ), &bMirror ) )
            session.setMirrorEditing( bMirror );
        if ( session.isMirrorEditing() )
        {
            ImGui::SameLine();
            int32 axis = static_cast<int32>( session.getMirrorAxis() );
            EditorChrome::setNextItemWidthColumns( 8.0f );
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
        MeshModifierStack& stack      = session.getStack();
        bool               bRecompute = stack.isRecomputingNormals();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "RecomputeNormals" ), &bRecompute ) )
        {
            session.recordUndo();
            stack.setRecomputingNormals( bRecompute );
            session.markDirty();
        }
        // 셰이딩 미리보기 - 켜진 모디파이어가 모두 GPU 표에 있으면 GPU 모프로, 아니면 CPU 결과 정점으로 셰이딩해 보인다. CPU 로 내려가면 무엇 때문인지 알린다
        bool bShadedPreview = session.isShadedPreviewEnabled();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "ShadedPreview" ), &bShadedPreview ) )
            session.setShadedPreviewEnabled( bShadedPreview );
        EditorSelfTestMarks::note( "meshmodifiers.shadedPreview" );
        MeshModifiersPanelInternal::drawItemTooltip( SW_LOCKEY( "EdMeshModifiers", "ShadedPreviewTooltip" ) );
        const bool bGPUPreviewBlocked = session.isShadedPreviewEnabled() && session.isGPUPreviewActive() == false && stack.getModifierCount() > 0;
        if ( bGPUPreviewBlocked )
        {
            ImGui::SameLine();
            const MeshModifier* pBlocking = stack.findModifier( session.getGPUPreviewBlockingIndex() );
            if ( pBlocking != nullptr )
                ImGui::TextDisabled( "%s %s", SW_LOCKEY( "EdMeshModifiers", "GPUPreviewBlockedBy" ), pBlocking->getName().c_str() );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdMeshModifiers", "GPUPreviewBlockedByStack" ) );
        }
        float32 time = session.getPreviewTime();
        if ( ImGui::DragFloat( SW_LOCKEY( "EdMeshModifiers", "PreviewTime" ), &time, 0.01f, 0.0f, MeshModifierSession::kMaxPreviewTimeSeconds ) )
            session.setPreviewTime( time );
        // 재생하면 시간만 바뀌는 애니메이션 모디파이어(Waving · Ripple · Vertex Anim …)가 칸을 끌지 않아도 움직인다
        bool bPlaying = session.isPreviewPlaying();
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdMeshModifiers", "PlayPreviewTime" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdMeshModifiers", "PlayPreviewTime" ), &bPlaying ) )
            session.setPreviewPlaying( bPlaying );
        EditorSelfTestMarks::note( "meshmodifiers.playPreviewTime" );

        ImGui::BeginDisabled( session.canUndo() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kUndo, SW_LOCKEY( "EdMeshModifiers", "Undo" ) ) ) )
            (void)session.undo(); // 버튼이 켜져 있으면 기록이 있다
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( editoricon::kRedo, SW_LOCKEY( "EdMeshModifiers", "Redo" ) ) ) );
        ImGui::BeginDisabled( session.canRedo() == false );
        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kRedo, SW_LOCKEY( "EdMeshModifiers", "Redo" ) ) ) )
            (void)session.redo(); // 버튼이 켜져 있으면 기록이 있다
        ImGui::EndDisabled();

        if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kSave, SW_LOCKEY( "EdMeshModifiers", "SaveStack" ) ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 하나다(`saveDocument` → `saveSidecar`) — 실패는 saveSidecar 가 알린다
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdMeshModifiers", "ReloadStack" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdMeshModifiers", "ReloadStack" ) ) )
        {
            session.recordUndo();
            if ( session.loadSidecar() == false )
                SW_LOG_WARNING( "No mesh modifier stack next to %#", session.getMeshPath().c_str() );
        }
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdMeshModifiers", "Bake" ) ) );
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

    SW_EDITOR_PANEL( MeshModifiersPanel, MeshModifiersPanel::kPanelID, EditorPanelCategory::Custom, 9010 );
} // namespace sw::editor
