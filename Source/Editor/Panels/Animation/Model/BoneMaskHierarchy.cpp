#include "pch.h"

#include "Editor/Panels/Animation/Model/BoneMaskHierarchy.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Graph/AnimationBoneMask.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

namespace sw::editor
{
    void BoneMaskHierarchy::build( const Skeleton& skeleton, const AnimationBoneMaskDef& mask, vector<BoneMaskBoneRow>& outListRow, vector<hashed_string>& outListMissingBone )
    {
        outListRow.clear();
        outListMissingBone.clear();
        // 스켈레톤에 있는 항목만으로 런타임 규칙을 돌린다 — 없는 본이 하나라도 있으면 런타임은 마스크 전체를 버리므로 그 이름은 따로 알린다.
        AnimationBoneMaskDef known{};
        known._name = mask._name;
        for ( const AnimationBoneMaskEntry& entry : mask._listEntry )
        {
            if ( skeleton.findBoneIndex( entry._bone ).has_value() )
                known._listEntry.push_back( entry );
            else if ( VectorUtil::contains( outListMissingBone, entry._bone ) == false )
                outListMissingBone.push_back( entry._bone );
        }
        vector<float32> listWeight;
        (void)known.computeBoneWeights( skeleton, listWeight ); // 없는 본을 뺐으므로 실패하지 않는다

        // 출처는 런타임의 덮는 순서를 따른다 — "bones" 가 "roots" 를 덮고, "roots" 가 없으면 가까운 조상의 "roots" 값을 물려받는다("bones" 는 퍼지지 않는다).
        const uint32  boneCount = skeleton.getBoneCount();
        vector<uint8> listRootReaches( boneCount, SW_FALSE );
        outListRow.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const SkeletonBone& bone        = skeleton.getBone( boneIndex );
            BoneMaskBoneRow&    row         = outListRow[boneIndex];
            const bool          bHasParent  = bone._parentIndex >= 0;
            const uint32        parentIndex = bHasParent ? static_cast<uint32>( bone._parentIndex ) : 0u;
            row._bone                       = bone._name;
            row._parentIndex                = bone._parentIndex;
            row._depth                      = bHasParent ? outListRow[parentIndex]._depth + 1u : 0u;
            row._weight                     = listWeight[boneIndex];
            bool bHasRoot                   = false;
            bool bHasSingle                 = false;
            for ( const AnimationBoneMaskEntry& entry : known._listEntry )
            {
                if ( entry._bone != bone._name )
                    continue;
                bHasRoot   = bHasRoot || entry._bPropagate == SW_TRUE;
                bHasSingle = bHasSingle || entry._bPropagate == SW_FALSE;
            }
            const bool bAncestorReaches = bHasParent && listRootReaches[parentIndex] == SW_TRUE;
            listRootReaches[boneIndex]  = ( bHasRoot || bAncestorReaches ) ? SW_TRUE : SW_FALSE;
            if ( bHasSingle )
                row._source = BoneMaskWeightSource::Bone;
            else if ( bHasRoot )
                row._source = BoneMaskWeightSource::Subtree;
            else if ( bAncestorReaches )
                row._source = BoneMaskWeightSource::Inherited;
            else
                row._source = BoneMaskWeightSource::None;
        }
    }

    bool BoneMaskHierarchy::paintWeight( AnimationBoneMaskDef& inoutMask, const hashed_string& bone, float32 weight, bool bSubtree )
    {
        const float32 clamped    = MathUtil::saturate( weight );
        const uint8   bPropagate = bSubtree ? SW_TRUE : SW_FALSE;
        bool          bChanged   = false;
        bool          bFound     = false;
        // 자손까지 칠하면 이 본 하나의 항목은 그 값을 덮어 가리므로 지운다. 이 본 하나만 칠하면 자손에 퍼지는 값은 그대로 둔다.
        vector<AnimationBoneMaskEntry> listEntry;
        listEntry.reserve( inoutMask._listEntry.size() + 1 );
        for ( const AnimationBoneMaskEntry& entry : inoutMask._listEntry )
        {
            const bool bSameBone = entry._bone == bone;
            if ( bSameBone && bSubtree && entry._bPropagate == SW_FALSE )
            {
                bChanged = true;
                continue;
            }
            AnimationBoneMaskEntry kept = entry;
            if ( bSameBone && entry._bPropagate == bPropagate && bFound == false )
            {
                bFound       = true;
                bChanged     = bChanged || entry._weight != clamped;
                kept._weight = clamped;
            }
            listEntry.push_back( kept );
        }
        if ( bFound == false )
        {
            AnimationBoneMaskEntry added{};
            added._bone       = bone;
            added._weight     = clamped;
            added._bPropagate = bPropagate;
            listEntry.push_back( added );
            bChanged = true;
        }
        if ( bChanged )
            inoutMask._listEntry = std::move( listEntry );
        return bChanged;
    }

    bool BoneMaskHierarchy::resetBone( AnimationBoneMaskDef& inoutMask, const hashed_string& bone )
    {
        vector<AnimationBoneMaskEntry> listEntry;
        listEntry.reserve( inoutMask._listEntry.size() );
        for ( const AnimationBoneMaskEntry& entry : inoutMask._listEntry )
        {
            if ( entry._bone != bone )
                listEntry.push_back( entry );
        }
        // 항목이 없는 마스크는 로드 오류다 — 마지막 본의 항목은 지우지 않는다.
        const bool bNothingRemoved = listEntry.size() == inoutMask._listEntry.size();
        if ( bNothingRemoved || listEntry.empty() )
            return false;
        inoutMask._listEntry = std::move( listEntry );
        return true;
    }
} // namespace sw::editor
