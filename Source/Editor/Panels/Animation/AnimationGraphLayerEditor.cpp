#include "pch.h"

#include "Editor/Panels/Animation/AnimationGraphLayerEditor.h"

#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "AnimationGraphLayers" );

    namespace
    {
        struct AnimationGraphLayerEditorInternal
        {
            /** @brief 값 입력란 앞 이름의 폭입니다(DPI 배율 전). */
            static constexpr float32 kLabelWidth = 112.0f;
            /** @brief 새 마스크의 첫 항목입니다. 상체 마스크의 흔한 모양(spine 과 그 자손)이며, 사용자가 스켈레톤의 본 이름으로 고칩니다. */
            static constexpr const utf8* kNewMaskRootBone = "spine";
            /** @brief 클립 상태가 없는 그래프에서 새 레이어가 처음 가리키는 클립입니다. */
            static constexpr const utf8* kNewLayerClip = "Idle";

            static bool isNameTaken( const AnimationGraphAsset& graph, bool bLayer, const hashed_string& name )
            {
                return bLayer ? graph.findLayer( name ) != nullptr : graph.findBoneMask( name ) != nullptr;
            }

            /** @brief 목록에 없는 새 이름입니다(Layer, Layer2, …). */
            static hashed_string makeUniqueName( const AnimationGraphAsset& graph, bool bLayer )
            {
                const utf8*   pBase = bLayer ? "Layer" : "Mask";
                hashed_string name( pBase );
                for ( uint32 suffix = 2; isNameTaken( graph, bLayer, name ); ++suffix )
                {
                    fixed_string<constant::kMaxBuffer64> text;
                    formatstring( text.data(), text.capacity(), "%#%#", pBase, suffix );
                    name = hashed_string( text.c_str() );
                }
                return name;
            }

            /** @brief 새 레이어의 클립입니다. 그래프의 첫 클립 상태(블렌드 스페이스 · 포즈 노드가 아닌 상태 — 이름이 곧 클립)를 씁니다. */
            static hashed_string findFirstClipName( const AnimationGraphAsset& graph )
            {
                for ( const AnimationGraphNode& node : graph._listNode )
                {
                    const bool bClipState = node.hasBlendSpace() == false && node.hasPoseNode() == false && node._name.empty() == false;
                    if ( bClipState )
                        return hashed_string( node._name );
                }
                return hashed_string( kNewLayerClip );
            }

            /** @brief 그 마스크를 쓰는 첫 레이어입니다. 없으면 nullptr 입니다. */
            static const AnimationGraphLayerDef* findLayerUsingMask( const AnimationGraphAsset& graph, const hashed_string& maskName )
            {
                for ( const AnimationGraphLayerDef& layer : graph._listLayer )
                {
                    if ( layer._maskName == maskName )
                        return &layer;
                }
                return nullptr;
            }

            static void noteIndexedMark( const utf8* pPrefix, size_t index )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                fixed_string<constant::kMaxBuffer64> key;
                formatstring( key.data(), key.capacity(), "%#.%#", pPrefix, static_cast<uint64>( index ) );
                EditorSelfTestMarks::note( key.c_str() );
            }

            static void drawRowLabel( const utf8* pLabel )
            {
                EditorWidgets::drawPropertyRowLabel( pLabel, kLabelWidth * EditorThemeUtil::getDpiScale() );
            }

            /** @brief 목록 순서를 한 항목 옮깁니다. @p index 와 @p index + @p step 이 범위 안이어야 합니다. */
            template <typename T>
            static void swapItems( vector<T>& inoutList, int32 index, int32 step )
            {
                std::swap( inoutList[static_cast<size_t>( index )], inoutList[static_cast<size_t>( index + step )] );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    AnimationGraphLayerEditor::AnimationGraphLayerEditor()
        : _mapEditBuffer{}
        , _selectedLayer{ -1 }
        , _selectedMask{ -1 }
    {
    }

    void AnimationGraphLayerEditor::clearSelection()
    {
        _selectedLayer = -1;
        _selectedMask  = -1;
        _mapEditBuffer.clear();
    }

    const utf8* AnimationGraphLayerEditor::draw( AnimationGraphAsset& inoutGraph )
    {
        // 다른 문서 · Undo 로 목록이 줄었으면 고른 순번을 놓는다.
        if ( _selectedLayer >= static_cast<int32>( inoutGraph._listLayer.size() ) )
            _selectedLayer = -1;
        if ( _selectedMask >= static_cast<int32>( inoutGraph._listBoneMask.size() ) )
            _selectedMask = -1;
        const utf8* pLayerEdit = drawLayers( inoutGraph );
        const utf8* pMaskEdit  = drawBoneMasks( inoutGraph );
        return pLayerEdit != nullptr ? pLayerEdit : pMaskEdit;
    }

    bool AnimationGraphLayerEditor::drawCommittedText( const utf8* pID, const utf8* pCurrent, string& outCommitted )
    {
        const uint32 itemID = ImGui::GetID( pID );
        const auto   it     = _mapEditBuffer.find( itemID );
        string       shown{ pCurrent };
        string&      text = it != _mapEditBuffer.end() ? it->second : shown;
        EditorWidgets::drawTextField( pID, text, -1.0f );
        if ( ImGui::IsItemActivated() )
            _mapEditBuffer[itemID] = text;
        if ( ImGui::IsItemDeactivated() == false )
            return false;
        const auto itDone = _mapEditBuffer.find( itemID );
        if ( itDone == _mapEditBuffer.end() )
            return false;
        const bool bCommitted = ImGui::IsItemDeactivatedAfterEdit();
        if ( bCommitted )
            outCommitted = itDone->second;
        _mapEditBuffer.erase( itDone );
        return bCommitted;
    }

    const utf8* AnimationGraphLayerEditor::drawLayers( AnimationGraphAsset& inoutGraph )
    {
        const utf8* pEdit = drawLayerList( inoutGraph );

        vector<AnimationGraphLayerDef>& listLayer = inoutGraph._listLayer;
        if ( _selectedLayer < 0 || _selectedLayer >= static_cast<int32>( listLayer.size() ) )
            return pEdit;
        AnimationGraphLayerDef& layer = listLayer[static_cast<size_t>( _selectedLayer )];
        ImGui::PushID( "layerDetail" );
        drawLayerIdentityRows( inoutGraph, layer, pEdit );
        drawLayerBlendRows( layer, pEdit );
        ImGui::PopID();
        return pEdit;
    }

    const utf8* AnimationGraphLayerEditor::drawLayerList( AnimationGraphAsset& inoutGraph )
    {
        const utf8*                     pEdit     = nullptr;
        vector<AnimationGraphLayerDef>& listLayer = inoutGraph._listLayer;
        ImGui::SeparatorText( "Layers" );
        if ( listLayer.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationGraph", "NoLayersHint" ) );
        for ( size_t index = 0; index < listLayer.size(); ++index )
        {
            const AnimationGraphLayerDef&         layer = listLayer[index];
            fixed_string<constant::kMaxBuffer128> label;
            formatstring( label.data(), label.capacity(), "%#  (%#, %#)", layer._name.c_str(), layer._clipName.c_str(),
                          layer._maskName.empty() ? "all bones" : layer._maskName.c_str() );
            ImGui::PushID( static_cast<int32>( index ) );
            if ( ImGui::Selectable( label.c_str(), _selectedLayer == static_cast<int32>( index ) ) )
                _selectedLayer = static_cast<int32>( index );
            AnimationGraphLayerEditorInternal::noteIndexedMark( "animationGraph.layer", index );
            ImGui::PopID();
        }

        if ( ImGui::Button( "Add Layer" ) )
        {
            AnimationGraphLayerDef layer{};
            layer._name     = AnimationGraphLayerEditorInternal::makeUniqueName( inoutGraph, true );
            layer._clipName = AnimationGraphLayerEditorInternal::findFirstClipName( inoutGraph );
            listLayer.push_back( layer );
            _selectedLayer = static_cast<int32>( listLayer.size() ) - 1;
            pEdit          = "Add Animation Layer";
        }
        EditorSelfTestMarks::note( "animationGraph.layer.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddLayerTip" ) );
        const int32 layerCount = static_cast<int32>( listLayer.size() );
        const bool  bSelected  = 0 <= _selectedLayer && _selectedLayer < layerCount;
        ImGui::BeginDisabled( bSelected == false );
        ImGui::SameLine();
        if ( ImGui::Button( "Remove##layer" ) )
        {
            listLayer.erase( listLayer.begin() + _selectedLayer );
            _selectedLayer = -1;
            pEdit          = "Remove Animation Layer";
        }
        EditorSelfTestMarks::note( "animationGraph.layer.remove" );
        ImGui::SameLine();
        // 레이어는 적은 순서대로 얹으므로 순서가 결과를 바꾼다(뒤의 레이어가 위다).
        ImGui::BeginDisabled( bSelected == false || _selectedLayer == 0 );
        if ( ImGui::ArrowButton( "##layerUp", ImGuiDir_Up ) )
        {
            AnimationGraphLayerEditorInternal::swapItems( listLayer, _selectedLayer, -1 );
            --_selectedLayer;
            pEdit = "Reorder Animation Layers";
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( bSelected == false || _selectedLayer + 1 >= layerCount );
        if ( ImGui::ArrowButton( "##layerDown", ImGuiDir_Down ) )
        {
            AnimationGraphLayerEditorInternal::swapItems( listLayer, _selectedLayer, 1 );
            ++_selectedLayer;
            pEdit = "Reorder Animation Layers";
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerOrderTip" ) );
        return pEdit;
    }

    void AnimationGraphLayerEditor::drawLayerIdentityRows( AnimationGraphAsset& inoutGraph, AnimationGraphLayerDef& inoutLayer, const utf8*& pInoutEdit )
    {
        string committed;
        AnimationGraphLayerEditorInternal::drawRowLabel( "Name" );
        if ( drawCommittedText( "##layerName", inoutLayer._name.c_str(), committed ) )
        {
            const hashed_string name( committed.c_str() );
            const bool          bTaken = name != inoutLayer._name && inoutGraph.findLayer( name ) != nullptr;
            if ( committed.empty() || bTaken )
            {
                SW_LOG_WARNING( "Layer name '%#' is empty or already used - the name stays '%#'", committed.c_str(), inoutLayer._name.c_str() );
            }
            else if ( name != inoutLayer._name )
            {
                inoutLayer._name = name;
                pInoutEdit       = "Rename Animation Layer";
            }
        }
        AnimationGraphLayerEditorInternal::drawRowLabel( "Clip" );
        if ( drawCommittedText( "##layerClip", inoutLayer._clipName.c_str(), committed ) )
        {
            if ( committed.empty() )
            {
                SW_LOG_WARNING( "A layer needs a clip - the clip stays '%#'", inoutLayer._clipName.c_str() );
            }
            else
            {
                inoutLayer._clipName = hashed_string( committed.c_str() );
                pInoutEdit           = "Edit Animation Layer";
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerClipTip" ) );

        AnimationGraphLayerEditorInternal::drawRowLabel( "Mask" );
        if ( ImGui::BeginCombo( "##layerMask", inoutLayer._maskName.empty() ? "(all bones)" : inoutLayer._maskName.c_str() ) )
        {
            if ( ImGui::Selectable( "(all bones)", inoutLayer._maskName.empty() ) )
            {
                inoutLayer._maskName = hashed_string{};
                pInoutEdit           = "Edit Animation Layer";
            }
            for ( const AnimationBoneMaskDef& mask : inoutGraph._listBoneMask )
            {
                if ( ImGui::Selectable( mask._name.c_str(), inoutLayer._maskName == mask._name ) )
                {
                    inoutLayer._maskName = mask._name;
                    pInoutEdit           = "Edit Animation Layer";
                }
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerMaskTip" ) );
    }

    void AnimationGraphLayerEditor::drawLayerBlendRows( AnimationGraphLayerDef& inoutLayer, const utf8*& pInoutEdit )
    {
        string committed;
        AnimationGraphLayerEditorInternal::drawRowLabel( "Weight" );
        ImGui::SliderFloat( "##layerWeight", &inoutLayer._weight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pInoutEdit = "Edit Animation Layer";
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerWeightTip" ) );
        AnimationGraphLayerEditorInternal::drawRowLabel( "Weight Param" );
        // 빈 hashed_string 의 c_str 는 "None" 이다 — 그대로 보이면 그 글이 파라미터 이름으로 저장된다.
        if ( drawCommittedText( "##layerWeightParam", inoutLayer._weightParameter.empty() ? "" : inoutLayer._weightParameter.c_str(), committed ) )
        {
            inoutLayer._weightParameter = hashed_string( committed.c_str() );
            pInoutEdit                  = "Edit Animation Layer";
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerWeightParamTip" ) );

        AnimationGraphLayerEditorInternal::drawRowLabel( "Blend" );
        if ( ImGui::BeginCombo( "##layerBlend", AnimationGraphAsset::getLayerBlendText( inoutLayer._blend ) ) )
        {
            for ( const AnimationLayerBlend blend : { AnimationLayerBlend::Override, AnimationLayerBlend::Additive } )
            {
                if ( ImGui::Selectable( AnimationGraphAsset::getLayerBlendText( blend ), inoutLayer._blend == blend ) )
                {
                    inoutLayer._blend = blend;
                    pInoutEdit        = "Edit Animation Layer";
                }
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerBlendTip" ) );
        AnimationGraphLayerEditorInternal::drawRowLabel( "Loop" );
        bool bLoop = inoutLayer._bLoop == SW_TRUE;
        if ( ImGui::Checkbox( "##layerLoop", &bLoop ) )
        {
            inoutLayer._bLoop = bLoop ? SW_TRUE : SW_FALSE;
            pInoutEdit        = "Edit Animation Layer";
        }
        AnimationGraphLayerEditorInternal::drawRowLabel( "Half Life" );
        ImGui::DragFloat( "##layerHalfLife", &inoutLayer._halfLifeSeconds, 0.01f, 0.0f, 2.0f, inoutLayer._halfLifeSeconds > 0.0f ? "%.2f s" : "(immediate)",
                          ImGuiSliderFlags_AlwaysClamp );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pInoutEdit = "Edit Animation Layer";
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerHalfLifeTip" ) );
    }

    const utf8* AnimationGraphLayerEditor::drawBoneMasks( AnimationGraphAsset& inoutGraph )
    {
        const utf8* pEdit = drawMaskList( inoutGraph );

        vector<AnimationBoneMaskDef>& listMask = inoutGraph._listBoneMask;
        if ( _selectedMask < 0 || _selectedMask >= static_cast<int32>( listMask.size() ) )
            return pEdit;
        AnimationBoneMaskDef& mask = listMask[static_cast<size_t>( _selectedMask )];
        ImGui::PushID( "maskDetail" );
        drawMaskSourceRows( inoutGraph, mask, pEdit );
        drawMaskEntries( mask, pEdit );
        ImGui::PopID();
        return pEdit;
    }

    const utf8* AnimationGraphLayerEditor::drawMaskList( AnimationGraphAsset& inoutGraph )
    {
        const utf8*                   pEdit    = nullptr;
        vector<AnimationBoneMaskDef>& listMask = inoutGraph._listBoneMask;
        ImGui::SeparatorText( "Bone Masks" );
        if ( listMask.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationGraph", "NoBoneMasksHint" ) );
        for ( size_t index = 0; index < listMask.size(); ++index )
        {
            const AnimationBoneMaskDef&           mask = listMask[index];
            fixed_string<constant::kMaxBuffer128> label;
            formatstring( label.data(), label.capacity(), "%#  (bones %#%#)", mask._name.c_str(), static_cast<uint64>( mask._listEntry.size() ),
                          mask._filePath.empty() ? "" : ", preset" );
            ImGui::PushID( static_cast<int32>( index ) );
            if ( ImGui::Selectable( label.c_str(), _selectedMask == static_cast<int32>( index ) ) )
                _selectedMask = static_cast<int32>( index );
            AnimationGraphLayerEditorInternal::noteIndexedMark( "animationGraph.mask", index );
            ImGui::PopID();
        }

        if ( ImGui::Button( "Add Mask" ) )
        {
            AnimationBoneMaskDef   mask{};
            AnimationBoneMaskEntry entry{};
            entry._bone   = hashed_string( AnimationGraphLayerEditorInternal::kNewMaskRootBone );
            entry._weight = 1.0f;
            mask._name    = AnimationGraphLayerEditorInternal::makeUniqueName( inoutGraph, false );
            mask._listEntry.push_back( entry );
            listMask.push_back( std::move( mask ) );
            _selectedMask = static_cast<int32>( listMask.size() ) - 1;
            pEdit         = "Add Bone Mask";
        }
        EditorSelfTestMarks::note( "animationGraph.mask.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddBoneMaskTip" ) );
        const bool                    bSelected = 0 <= _selectedMask && _selectedMask < static_cast<int32>( listMask.size() );
        const AnimationGraphLayerDef* pUser =
            bSelected ? AnimationGraphLayerEditorInternal::findLayerUsingMask( inoutGraph, listMask[static_cast<size_t>( _selectedMask )]._name ) : nullptr;
        // 레이어가 쓰는 마스크를 지우면 저장한 그래프가 로드 오류가 된다 — 그 레이어의 마스크를 먼저 바꾼다.
        ImGui::SameLine();
        ImGui::BeginDisabled( bSelected == false || pUser != nullptr );
        if ( ImGui::Button( "Remove##mask" ) )
        {
            listMask.erase( listMask.begin() + _selectedMask );
            _selectedMask = -1;
            pEdit         = "Remove Bone Mask";
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( "animationGraph.mask.remove" );
        if ( pUser != nullptr )
            ImGui::SetItemTooltip( "Used by layer '%s'", pUser->_name.c_str() );
        return pEdit;
    }

    void AnimationGraphLayerEditor::drawMaskSourceRows( AnimationGraphAsset& inoutGraph, AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit )
    {
        string committed;
        AnimationGraphLayerEditorInternal::drawRowLabel( "Name" );
        if ( drawCommittedText( "##maskName", inoutMask._name.c_str(), committed ) )
        {
            const hashed_string name( committed.c_str() );
            const bool          bTaken = name != inoutMask._name && inoutGraph.findBoneMask( name ) != nullptr;
            if ( committed.empty() || bTaken )
            {
                SW_LOG_WARNING( "Bone mask name '%#' is empty or already used - the name stays '%#'", committed.c_str(), inoutMask._name.c_str() );
            }
            else if ( name != inoutMask._name )
            {
                // 이름이 곧 레이어의 참조다 — 그 마스크를 쓰던 레이어도 함께 옮긴다.
                for ( AnimationGraphLayerDef& layer : inoutGraph._listLayer )
                {
                    if ( layer._maskName == inoutMask._name )
                        layer._maskName = name;
                }
                inoutMask._name = name;
                pInoutEdit      = "Rename Bone Mask";
            }
        }

        // 프리셋 파일을 고르면 그 파일을 읽어 항목을 채우고, 비우면 지금 항목을 그래프에 바로 적는다.
        string presetPath = inoutMask._filePath;
        if ( EditorWidgets::drawAssetSlot( "Preset", presetPath, ".bonemask.json", AnimationGraphLayerEditorInternal::kLabelWidth * EditorThemeUtil::getDpiScale() ) )
        {
            AnimationBoneMaskDef loaded{};
            loaded._name = inoutMask._name;
            if ( presetPath.empty() )
            {
                inoutMask._filePath.clear();
                pInoutEdit = "Embed Bone Mask";
            }
            else if ( loaded.loadFromFile( presetPath ) )
            {
                inoutMask  = std::move( loaded );
                pInoutEdit = "Use Bone Mask Preset";
            }
            else
            {
                SW_LOG_WARNING( "Bone mask preset '%#' could not be read - the mask is unchanged", presetPath.c_str() );
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "BoneMaskPresetTip" ) );
    }

    void AnimationGraphLayerEditor::drawMaskEntries( AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit )
    {
        // 프리셋의 항목은 그 파일이 원본이다 — 여기서 고치면 저장할 때 버려지므로 보이기만 한다.
        const bool bPreset = inoutMask._filePath.empty() == false;
        ImGui::BeginDisabled( bPreset );
        string committed;
        size_t removeIndex = inoutMask._listEntry.size();
        if ( ImGui::BeginTable( "##maskEntries", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg ) )
        {
            ImGui::TableSetupColumn( "Bone", ImGuiTableColumnFlags_WidthStretch, 3.0f );
            ImGui::TableSetupColumn( "Weight", ImGuiTableColumnFlags_WidthStretch, 2.0f );
            ImGui::TableSetupColumn( "Children", ImGuiTableColumnFlags_WidthFixed );
            ImGui::TableSetupColumn( "##remove", ImGuiTableColumnFlags_WidthFixed );
            ImGui::TableHeadersRow();
            for ( size_t entryIndex = 0; entryIndex < inoutMask._listEntry.size(); ++entryIndex )
            {
                AnimationBoneMaskEntry& entry = inoutMask._listEntry[entryIndex];
                ImGui::PushID( static_cast<int32>( entryIndex ) );
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if ( drawCommittedText( "##bone", entry._bone.c_str(), committed ) )
                {
                    if ( committed.empty() )
                    {
                        SW_LOG_WARNING( "A bone mask entry needs a bone name - it stays '%#'", entry._bone.c_str() );
                    }
                    else
                    {
                        entry._bone = hashed_string( committed.c_str() );
                        pInoutEdit  = "Edit Bone Mask";
                    }
                }
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth( -FLT_MIN );
                ImGui::SliderFloat( "##weight", &entry._weight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    pInoutEdit = "Edit Bone Mask";
                ImGui::TableNextColumn();
                bool bPropagate = entry._bPropagate == SW_TRUE;
                if ( ImGui::Checkbox( "##children", &bPropagate ) )
                {
                    entry._bPropagate = bPropagate ? SW_TRUE : SW_FALSE;
                    pInoutEdit        = "Edit Bone Mask";
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "BoneMaskChildrenTip" ) );
                ImGui::TableNextColumn();
                // 항목이 없는 마스크는 로드 오류다 — 마지막 하나는 지우지 못한다.
                ImGui::BeginDisabled( inoutMask._listEntry.size() <= 1 );
                if ( ImGui::SmallButton( "x" ) )
                    removeIndex = entryIndex;
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if ( removeIndex < inoutMask._listEntry.size() )
        {
            inoutMask._listEntry.erase( inoutMask._listEntry.begin() + static_cast<ptrdiff_t>( removeIndex ) );
            pInoutEdit = "Edit Bone Mask";
        }
        if ( ImGui::Button( "Add Bone" ) )
        {
            AnimationBoneMaskEntry entry = inoutMask._listEntry.back();
            inoutMask._listEntry.push_back( entry );
            pInoutEdit = "Edit Bone Mask";
        }
        EditorSelfTestMarks::note( "animationGraph.mask.addBone" );
        ImGui::EndDisabled();
    }
} // namespace sw::editor
