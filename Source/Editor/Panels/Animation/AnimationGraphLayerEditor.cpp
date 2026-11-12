#include "pch.h"

#include "Editor/Panels/Animation/AnimationGraphLayerEditor.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Animation/Model/AnimationGraphLayerEdit.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct AnimationGraphLayerEditorInternal
        {
            /** @brief 값 입력란 앞 이름의 폭입니다(DPI 배율 전). */
            static void recordIndexedMark( const utf8* pPrefix, size_t index )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                fixed_string<constant::buffer::kMax64> key;
                formatstring( key.data(), key.capacity(), "%#.%#", pPrefix, static_cast<uint64>( index ) );
                EditorSelfTestMarks::record( key.c_str() );
            }

            static void drawRowLabel( const utf8* pLabel )
            {
                EditorWidgets::drawPropertyRowLabel( pLabel );
            }

            /** @brief 계층 보기가 보이는 본 줄 수입니다(그 아래는 스크롤). */
            static constexpr float32 kBoneTreeLines = 12.0f;
            /** @brief 가중치 막대의 폭(글자 칸)입니다. */
            static constexpr float32 kWeightBarColumns = 4.0f;

            /** @brief 출처를 한 문장으로 풉니다(줄의 툴팁 · 고른 본의 설명). */
            static const utf8* describeSource( BoneMaskWeightSource source )
            {
                switch ( source )
                {
                    case BoneMaskWeightSource::None:
                        return SW_LOCKEY( "EditorAnimationGraph", "MaskSourceNone" );
                    case BoneMaskWeightSource::Inherited:
                        return SW_LOCKEY( "EditorAnimationGraph", "MaskSourceInherited" );
                    case BoneMaskWeightSource::Subtree:
                        return SW_LOCKEY( "EditorAnimationGraph", "MaskSourceSubtree" );
                    case BoneMaskWeightSource::Bone:
                        return SW_LOCKEY( "EditorAnimationGraph", "MaskSourceBone" );
                }
                return "";
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
        , _skeletonPath{}
        , _pSkeleton{}
        , _listBoneRow{}
        , _listMissingBone{}
        , _selectedBone{}
        , _paintWeight{ 1.0f }
    {
    }

    void AnimationGraphLayerEditor::clearSelection()
    {
        _selectedLayer = -1;
        _selectedMask  = -1;
        _selectedBone  = hashed_string{};
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
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Layers" ) );
        if ( listLayer.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationGraph", "NoLayersHint" ) );
        for ( size_t index = 0; index < listLayer.size(); ++index )
        {
            const AnimationGraphLayerDef&           layer = listLayer[index];
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "%#  (%#, %#)", layer._name.c_str(), layer._slotName.empty() ? layer._clipName.c_str() : layer._slotName.c_str(),
                          layer._maskName.empty() ? "all bones" : layer._maskName.c_str() );
            ImGui::PushID( static_cast<int32>( index ) );
            if ( ImGui::Selectable( label.c_str(), _selectedLayer == static_cast<int32>( index ) ) )
                _selectedLayer = static_cast<int32>( index );
            AnimationGraphLayerEditorInternal::recordIndexedMark( "animationGraph.layer", index );
            ImGui::PopID();
        }

        if ( ImGui::Button( SW_LOCKEY( "EdText", "AddLayer" ) ) )
        {
            _selectedLayer = AnimationGraphLayerEdit::addLayer( inoutGraph );
            pEdit          = "Add Animation Layer";
        }
        EditorSelfTestMarks::record( "animationGraph.layer.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddLayerTip" ) );
        const int32 layerCount = static_cast<int32>( listLayer.size() );
        const bool  bSelected  = 0 <= _selectedLayer && _selectedLayer < layerCount;
        ImGui::BeginDisabled( bSelected == false );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove##layer" ) );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Remove" ), "Remove##layer" ) ) )
        {
            listLayer.erase( listLayer.begin() + _selectedLayer );
            _selectedLayer = -1;
            pEdit          = "Remove Animation Layer";
        }
        EditorSelfTestMarks::record( "animationGraph.layer.remove" );
        // 레이어는 적은 순서대로 얹으므로 순서가 결과를 바꾼다(뒤의 레이어가 위다).
        ImGui::BeginDisabled( bSelected == false || _selectedLayer == 0 );
        EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
        if ( ImGui::ArrowButton( "##layerUp", ImGuiDir_Up ) )
        {
            _selectedLayer = AnimationGraphLayerEdit::moveLayer( inoutGraph, _selectedLayer, -1 );
            pEdit          = "Reorder Animation Layers";
        }
        ImGui::EndDisabled();
        EditorChrome::sameLineIfFits( EditorChrome::measureArrowButton() );
        ImGui::BeginDisabled( bSelected == false || _selectedLayer + 1 >= layerCount );
        if ( ImGui::ArrowButton( "##layerDown", ImGuiDir_Down ) )
        {
            _selectedLayer = AnimationGraphLayerEdit::moveLayer( inoutGraph, _selectedLayer, 1 );
            pEdit          = "Reorder Animation Layers";
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerOrderTip" ) );
        return pEdit;
    }

    void AnimationGraphLayerEditor::drawLayerIdentityRows( AnimationGraphAsset& inoutGraph, AnimationGraphLayerDef& inoutLayer, const utf8*& pInoutEdit )
    {
        string committed;
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Name" ) );
        if ( drawCommittedText( "##layerName", inoutLayer._name.c_str(), committed ) )
        {
            const hashed_string name( committed.c_str() );
            const bool          bTaken = name != inoutLayer._name && inoutGraph.findLayer( name ) != nullptr;
            if ( committed.empty() || bTaken )
            {
                SW_LOG_WARNING_ID( "AnimationGraphLayers.LayerNameEmptyAlready", "Layer name '%#' is empty or already used - the name stays '%#'", committed.c_str(), inoutLayer._name.c_str() );
            }
            else if ( name != inoutLayer._name )
            {
                inoutLayer._name = name;
                pInoutEdit       = "Rename Animation Layer";
            }
        }
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Clip" ) );
        if ( drawCommittedText( "##layerClip", inoutLayer._clipName.c_str(), committed ) )
        {
            if ( committed.empty() )
            {
                SW_LOG_WARNING_ID( "AnimationGraphLayers.LayerNeedsClipClip", "A layer needs a clip or a slot - the clip stays '%#'", inoutLayer._clipName.c_str() );
            }
            else
            {
                inoutLayer._clipName = hashed_string( committed.c_str() );
                inoutLayer._slotName = hashed_string{}; // 레이어는 클립과 슬롯 중 하나만 든다
                pInoutEdit           = "Edit Animation Layer";
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerClipTip" ) );
        AnimationGraphLayerEditorInternal::drawRowLabel( "Slot" );
        if ( drawCommittedText( "##layerSlot", inoutLayer._slotName.c_str(), committed ) && committed.empty() == false )
        {
            inoutLayer._slotName = hashed_string( committed.c_str() );
            inoutLayer._clipName = hashed_string{};
            pInoutEdit           = "Edit Animation Layer";
        }

        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Mask" ) );
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
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Weight" ) );
        ImGui::SliderFloat( "##layerWeight", &inoutLayer._weight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            pInoutEdit = "Edit Animation Layer";
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerWeightTip" ) );
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "WeightParam" ) );
        if ( drawCommittedText( "##layerWeightParam", inoutLayer._weightParameter.c_str(), committed ) )
        {
            inoutLayer._weightParameter = hashed_string( committed.c_str() );
            pInoutEdit                  = "Edit Animation Layer";
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerWeightParamTip" ) );

        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Blend" ) );
        if ( ImGui::BeginCombo( "##layerBlend", getEnumName( inoutLayer._blend ) ) )
        {
            for ( const AnimationLayerBlend blend : { AnimationLayerBlend::Override, AnimationLayerBlend::Additive, AnimationLayerBlend::OverrideMeshSpace } )
            {
                if ( ImGui::Selectable( getEnumName( blend ), inoutLayer._blend == blend ) )
                {
                    inoutLayer._blend = blend;
                    pInoutEdit        = "Edit Animation Layer";
                }
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LayerBlendTip" ) );
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Loop" ) );
        bool bLoop = inoutLayer._bLoop == SW_TRUE;
        if ( ImGui::Checkbox( "##layerLoop", &bLoop ) )
        {
            inoutLayer._bLoop = bLoop ? SW_TRUE : SW_FALSE;
            pInoutEdit        = "Edit Animation Layer";
        }
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "HalfLife" ) );
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
        drawMaskHierarchy( mask, pEdit );
        ImGui::PopID();
        return pEdit;
    }

    const utf8* AnimationGraphLayerEditor::drawMaskList( AnimationGraphAsset& inoutGraph )
    {
        const utf8*                   pEdit    = nullptr;
        vector<AnimationBoneMaskDef>& listMask = inoutGraph._listBoneMask;
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "BoneMasks" ) );
        if ( listMask.empty() )
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationGraph", "NoBoneMasksHint" ) );
        for ( size_t index = 0; index < listMask.size(); ++index )
        {
            const AnimationBoneMaskDef&             mask = listMask[index];
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "%#  (bones %#%#)", mask._name.c_str(), static_cast<uint64>( mask._listEntry.size() ),
                          mask._filePath.empty() ? "" : ", preset" );
            ImGui::PushID( static_cast<int32>( index ) );
            if ( ImGui::Selectable( label.c_str(), _selectedMask == static_cast<int32>( index ) ) )
                _selectedMask = static_cast<int32>( index );
            AnimationGraphLayerEditorInternal::recordIndexedMark( "animationGraph.mask", index );
            ImGui::PopID();
        }

        if ( ImGui::Button( SW_LOCKEY( "EdText", "AddMask" ) ) )
        {
            _selectedMask = AnimationGraphLayerEdit::addMask( inoutGraph );
            pEdit         = "Add Bone Mask";
        }
        EditorSelfTestMarks::record( "animationGraph.mask.add" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddBoneMaskTip" ) );
        const bool                    bSelected = 0 <= _selectedMask && _selectedMask < static_cast<int32>( listMask.size() );
        const AnimationGraphLayerDef* pUser =
            bSelected ? AnimationGraphLayerEdit::findLayerUsingMask( inoutGraph, listMask[static_cast<size_t>( _selectedMask )]._name ) : nullptr;
        // 레이어가 쓰는 마스크를 지우면 저장한 그래프가 로드 오류가 된다 — 그 레이어의 마스크를 먼저 바꾼다.
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove##mask" ) );
        ImGui::BeginDisabled( bSelected == false || pUser != nullptr );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Remove" ), "Remove##mask" ) ) && AnimationGraphLayerEdit::removeMask( inoutGraph, _selectedMask ) )
        {
            _selectedMask = -1;
            pEdit         = "Remove Bone Mask";
        }
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "animationGraph.mask.remove" );
        if ( pUser != nullptr )
            ImGui::SetItemTooltip( "%s", SW_LOCKEYFORMAT( "EdFormat", "UsedByLayer", TextArgumentList().addText( "layer", pUser->_name.c_str() ) ).c_str() );
        return pEdit;
    }

    void AnimationGraphLayerEditor::drawMaskSourceRows( AnimationGraphAsset& inoutGraph, AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit )
    {
        string committed;
        AnimationGraphLayerEditorInternal::drawRowLabel( SW_LOCKEY( "EdWidget", "Name" ) );
        if ( drawCommittedText( "##maskName", inoutMask._name.c_str(), committed ) )
        {
            const hashed_string name( committed.c_str() );
            const bool          bTaken = name != inoutMask._name && inoutGraph.findBoneMask( name ) != nullptr;
            if ( committed.empty() || bTaken )
            {
                SW_LOG_WARNING_ID( "AnimationGraphLayers.BoneMaskNameEmpty", "Bone mask name '%#' is empty or already used - the name stays '%#'", committed.c_str(), inoutMask._name.c_str() );
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
        if ( EditorWidgets::drawAssetSlot( SW_LOCKEY( "EdWidget", "Preset" ), presetPath, ".bonemask.xml" ) )
        {
            const BoneMaskPresetResult result = AnimationGraphLayerEdit::applyMaskPreset( inoutMask, presetPath );
            if ( result == BoneMaskPresetResult::Embedded )
                pInoutEdit = "Embed Bone Mask";
            else if ( result == BoneMaskPresetResult::Loaded )
                pInoutEdit = "Use Bone Mask Preset";
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
                        SW_LOG_WARNING_ID( "AnimationGraphLayers.BoneMaskEntryNeeds", "A bone mask entry needs a bone name - it stays '%#'", entry._bone.c_str() );
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
        if ( ImGui::Button( SW_LOCKEY( "EdText", "AddBone" ) ) )
        {
            AnimationBoneMaskEntry entry = inoutMask._listEntry.back();
            inoutMask._listEntry.push_back( entry );
            pInoutEdit = "Edit Bone Mask";
        }
        EditorSelfTestMarks::record( "animationGraph.mask.addBone" );
        ImGui::EndDisabled();
    }

    void AnimationGraphLayerEditor::applySkeletonPath()
    {
        const string_view path( _skeletonPath.c_str() );
        _selectedBone = hashed_string{};
        // 읽지 못하면 공유 캐시가 이유를 남긴다 — 계층 보기는 비우고 안내 글을 보인다(빈 스켈레톤으로 보이지 않는다).
        _pSkeleton = path.empty() ? nullptr : SharedAssetCache<Skeleton>::acquire( path );
    }

    void AnimationGraphLayerEditor::drawBoneRow( const BoneMaskBoneRow& row, uint32 rowIndex )
    {
        using Internal = AnimationGraphLayerEditorInternal;
        ImGui::PushID( static_cast<int32>( rowIndex ) );
        const float32 indent    = ImGui::GetStyle().IndentSpacing * static_cast<float32>( row._depth );
        const float32 barWidth  = EditorChrome::measureColumns( Internal::kWeightBarColumns );
        const ImVec2  start     = ImGui::GetCursorScreenPos();
        const float32 height    = ImGui::GetTextLineHeight();
        const float32 barTop    = start.y + height * 0.2f;
        const float32 barBottom = start.y + height * 0.8f;
        // 가중치 막대는 데이터라 강조색이다(바탕 칸 위에 값만큼). 이름은 이 본의 항목이면 본문 색, 물려받았거나 닿지 않으면 흐린 색이다.
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddRectFilled( ImVec2( start.x, barTop ), ImVec2( start.x + barWidth, barBottom ), EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "FrameBg" ) ) );
        if ( row._weight > 0.0f )
            pDrawList->AddRectFilled( ImVec2( start.x, barTop ), ImVec2( start.x + barWidth * row._weight, barBottom ), EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Accent" ) ) );
        const bool                              bExplicit = row._source == BoneMaskWeightSource::Subtree || row._source == BoneMaskWeightSource::Bone;
        const int32                             percent   = static_cast<int32>( MathUtil::round( row._weight * 100.0f ) );
        fixed_string<constant::buffer::kMax128> label;
        formatstring( label.data(), label.capacity(), "%#  %#%%", row._bone.c_str(), percent );
        ImGui::SetCursorScreenPos( ImVec2( start.x + barWidth + ImGui::GetStyle().ItemSpacing.x + indent, start.y ) );
        if ( bExplicit == false )
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "TextMuted" ) ) );
        if ( ImGui::Selectable( label.c_str(), _selectedBone == row._bone ) )
            _selectedBone = row._bone;
        if ( bExplicit == false )
            ImGui::PopStyleColor();
        EditorWidgets::drawTooltip( Internal::describeSource( row._source ) );
        Internal::recordIndexedMark( "animationGraph.mask.bone", rowIndex );
        ImGui::PopID();
    }

    void AnimationGraphLayerEditor::drawMaskHierarchy( AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit )
    {
        using Internal = AnimationGraphLayerEditorInternal;
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "SkeletonView" ) );
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdWidget", "Skeleton" ), SW_LOCKEY( "EditorAnimationGraph", "MaskSkeletonTip" ) );
        if ( EditorWidgets::drawAssetPathField( "##maskSkeleton", _skeletonPath, "*.skeleton.xml", ".skeleton.xml", "animationGraph.mask.skeleton" )._bDeactivatedAfterEdit )
            applySkeletonPath();
        if ( _pSkeleton == nullptr )
        {
            _listBoneRow.clear();
            _listMissingBone.clear();
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationGraph", "MaskNoSkeletonHint" ) );
            return;
        }
        BoneMaskHierarchy::build( *_pSkeleton, inoutMask, _listBoneRow, _listMissingBone );
        // 스켈레톤에 없는 본은 런타임이 마스크를 버리는 오류다 — 색만이 아니라 이름과 함께 알린다.
        if ( _listMissingBone.empty() == false )
        {
            string names;
            for ( const hashed_string& bone : _listMissingBone )
            {
                if ( names.empty() == false )
                    names += ", ";
                names += bone.c_str();
            }
            ImGui::PushStyleColor( ImGuiCol_Text, EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) );
            ImGui::TextWrapped( "%s", SW_LOCKEYFORMAT( "EdFormat", "MaskMissingBones", TextArgumentList().addText( "bones", names.c_str() ) ).c_str() );
            ImGui::PopStyleColor();
        }
        const float32 treeHeight = ImGui::GetTextLineHeightWithSpacing() * Internal::kBoneTreeLines;
        if ( ImGui::BeginChild( "##maskBones", ImVec2( 0.0f, treeHeight ), ImGuiChildFlags_Borders ) )
        {
            for ( uint32 rowIndex = 0; rowIndex < static_cast<uint32>( _listBoneRow.size() ); ++rowIndex )
            {
                drawBoneRow( _listBoneRow[rowIndex], rowIndex );
            }
        }
        ImGui::EndChild();

        const BoneMaskBoneRow* pSelected = nullptr;
        for ( const BoneMaskBoneRow& row : _listBoneRow )
        {
            if ( row._bone == _selectedBone )
                pSelected = &row;
        }
        if ( pSelected == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAnimationGraph", "MaskPickBoneHint" ) );
            return;
        }
        ImGui::TextUnformatted( pSelected->_bone.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", Internal::describeSource( pSelected->_source ) );
        // 프리셋의 항목은 그 파일이 원본이다 — 칠하면 저장할 때 버려지므로 보이기만 한다.
        ImGui::BeginDisabled( inoutMask._filePath.empty() == false );
        Internal::drawRowLabel( SW_LOCKEY( "EdWidget", "Weight" ) );
        ImGui::SliderFloat( "##paintWeight", &_paintWeight, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp );
        EditorSelfTestMarks::record( "animationGraph.mask.paintWeight" );
        if ( ImGui::Button( SW_LOCKEY( "EditorAnimationGraph", "PaintSubtree" ) ) && BoneMaskHierarchy::paintWeight( inoutMask, _selectedBone, _paintWeight, true ) )
            pInoutEdit = "Paint Bone Mask";
        EditorSelfTestMarks::record( "animationGraph.mask.paintSubtree" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "PaintSubtreeTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAnimationGraph", "PaintBone" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorAnimationGraph", "PaintBone" ) ) && BoneMaskHierarchy::paintWeight( inoutMask, _selectedBone, _paintWeight, false ) )
            pInoutEdit = "Paint Bone Mask";
        EditorSelfTestMarks::record( "animationGraph.mask.paintBone" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "PaintBoneTip" ) );
        const bool bHasEntry = pSelected->_source == BoneMaskWeightSource::Subtree || pSelected->_source == BoneMaskWeightSource::Bone;
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reset" ) ) );
        ImGui::BeginDisabled( bHasEntry == false );
        if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdVerb", "Reset" ), "Reset##maskBone" ) ) && BoneMaskHierarchy::resetBone( inoutMask, _selectedBone ) )
            pInoutEdit = "Reset Bone Mask Bone";
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( "animationGraph.mask.resetBone" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "ResetBoneTip" ) );
        ImGui::EndDisabled();
    }
} // namespace sw::editor
