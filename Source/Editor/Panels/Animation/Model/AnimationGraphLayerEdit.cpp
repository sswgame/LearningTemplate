#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationGraphLayerEdit.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/fixed_string.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Content/AssetID.h"

namespace sw::editor
{
    namespace
    {
        struct AnimationGraphLayerEditInternal
        {
            /** @brief 새 마스크의 첫 항목입니다. 상체 마스크의 흔한 모양(spine 과 그 자손)이며 고쳐 쓴다. */
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
                    fixed_string<constant::buffer::kMax64> text;
                    formatstring( text.data(), text.capacity(), "%#%#", pBase, suffix );
                    name = hashed_string( text.c_str() );
                }
                return name;
            }

            /** @brief 새 레이어의 클립입니다. 그래프의 첫 클립 상태이고, 없으면 `kNewLayerClip` 입니다. */
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
        };
    } // namespace

    int32 AnimationGraphLayerEdit::addLayer( AnimationGraphAsset& inoutGraph )
    {
        using Internal = AnimationGraphLayerEditInternal;
        AnimationGraphLayerDef layer{};
        layer._name     = Internal::makeUniqueName( inoutGraph, true );
        layer._clipName = Internal::findFirstClipName( inoutGraph );
        inoutGraph._listLayer.push_back( layer );
        return static_cast<int32>( inoutGraph._listLayer.size() ) - 1;
    }

    int32 AnimationGraphLayerEdit::moveLayer( AnimationGraphAsset& inoutGraph, int32 index, int32 step )
    {
        const int32 count  = static_cast<int32>( inoutGraph._listLayer.size() );
        const int32 target = index + step;
        if ( index < 0 || index >= count || target < 0 || target >= count )
            return index;
        std::swap( inoutGraph._listLayer[static_cast<size_t>( index )], inoutGraph._listLayer[static_cast<size_t>( target )] );
        return target;
    }

    int32 AnimationGraphLayerEdit::addMask( AnimationGraphAsset& inoutGraph )
    {
        using Internal = AnimationGraphLayerEditInternal;
        AnimationBoneMaskDef   mask{};
        AnimationBoneMaskEntry entry{};
        entry._bone   = hashed_string( Internal::kNewMaskRootBone );
        entry._weight = 1.0f;
        mask._name    = Internal::makeUniqueName( inoutGraph, false );
        mask._listEntry.push_back( entry );
        inoutGraph._listBoneMask.push_back( std::move( mask ) );
        return static_cast<int32>( inoutGraph._listBoneMask.size() ) - 1;
    }

    const AnimationGraphLayerDef* AnimationGraphLayerEdit::findLayerUsingMask( const AnimationGraphAsset& graph, const hashed_string& maskName )
    {
        for ( const AnimationGraphLayerDef& layer : graph._listLayer )
        {
            if ( layer._maskName == maskName )
                return &layer;
        }
        return nullptr;
    }

    bool AnimationGraphLayerEdit::removeMask( AnimationGraphAsset& inoutGraph, int32 index )
    {
        const bool bInRange = 0 <= index && index < static_cast<int32>( inoutGraph._listBoneMask.size() );
        if ( bInRange == false || findLayerUsingMask( inoutGraph, inoutGraph._listBoneMask[static_cast<size_t>( index )]._name ) != nullptr )
            return false;
        inoutGraph._listBoneMask.erase( inoutGraph._listBoneMask.begin() + index );
        return true;
    }

    BoneMaskPresetResult AnimationGraphLayerEdit::applyMaskPreset( AnimationBoneMaskDef& inoutMask, string_view presetPath )
    {
        if ( presetPath.empty() )
        {
            inoutMask._filePath.clear();
            return BoneMaskPresetResult::Embedded;
        }
        AnimationBoneMaskDef loaded{};
        loaded._name = inoutMask._name;
        if ( loaded.loadFromAsset( AssetID{ presetPath } ) == false )
        {
            SW_LOG_WARNING_ID( "AnimationGraphLayers.BoneMaskPresetCould", "Bone mask preset '%#' could not be read - the mask is unchanged", presetPath );
            return BoneMaskPresetResult::Failed;
        }
        inoutMask = std::move( loaded );
        return BoneMaskPresetResult::Loaded;
    }
} // namespace sw::editor
