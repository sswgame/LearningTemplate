#include "pch.h"

#include "Engine/Animation/Graph/AnimationBoneMask.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/Graph/AnimationGraphParams.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct AnimationBoneMaskInternal
        {
            /** @brief 항목 목록 요소 이름입니다. 순서는 `_bPropagate` 의 참(roots), 거짓(bones)입니다. */
            static constexpr const utf8* kArrListKey[] = { "roots", "bones" };
            /** @brief 목록 안 항목 요소의 이름입니다. */
            static constexpr const utf8* kEntryTag = "entry";

            /** @brief 목록 요소 하나의 항목을 읽어 덧붙입니다. 요소가 없으면 항목이 없는 것입니다. 잘못되었으면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool parseEntries( const XMLNode& element, const utf8* pListKey, bool bPropagate, string_view context,
                                                    vector<AnimationBoneMaskEntry>& inoutListEntry )
            {
                const XMLNode listNode = element.findChild( pListKey );
                if ( listNode.isValid() == false )
                    return true;
                bool   bValid = true;
                size_t index{ 0 };
                for ( XMLNode entryNode = listNode.findChild(); entryNode.isValid(); entryNode = entryNode.findNextSibling(), ++index )
                {
                    if ( string_view{ entryNode.getName() } != kEntryTag )
                    {
                        SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskItemNot", "%#: bone mask '%#' item %# is not an <%#> element", context, pListKey, index, kEntryTag );
                        bValid = false;
                        continue;
                    }
                    AnimationBoneMaskEntryParams params;
                    if ( XMLFields::readFields( entryNode, params, context ) == false || XMLNameCheck::reportUnknownChildren( entryNode, nullptr, 0, context ) == false )
                    {
                        bValid = false;
                        continue;
                    }
                    if ( params._bone.empty() )
                    {
                        SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskItemNo", "%#: bone mask '%#' item %# has an empty 'bone'", context, pListKey, index );
                        bValid = false;
                        continue;
                    }
                    AnimationBoneMaskEntry entry{};
                    entry._bone       = params._bone;
                    entry._weight     = params._weight;
                    entry._bPropagate = bPropagate ? SW_TRUE : SW_FALSE;
                    inoutListEntry.push_back( entry );
                }
                return bValid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimationBoneMaskDef::parseObject( const XMLNode& element, string_view context )
    {
        _listEntry.clear();
        if ( element.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskMustElement", "%#: bone mask must be an element", context );
            return false;
        }
        // 알려진 이름은 목록 둘뿐이다. 그래프 안에 적을 때는 `name` · `file` 속성이 더해진다(`AnimationGraphAsset`) — 그 이름은 부르는 쪽이 이미 걸렀다.
        for ( XMLNode child = element.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const string_view childName{ child.getName() };
            if ( childName != AnimationBoneMaskInternal::kArrListKey[0] && childName != AnimationBoneMaskInternal::kArrListKey[1] )
            {
                SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskUnknownElement", "%#: bone mask has unknown element '%#'", context, child.getName() );
                return false;
            }
        }
        const bool bRootsValid = AnimationBoneMaskInternal::parseEntries( element, AnimationBoneMaskInternal::kArrListKey[0], true, context, _listEntry );
        const bool bBonesValid = AnimationBoneMaskInternal::parseEntries( element, AnimationBoneMaskInternal::kArrListKey[1], false, context, _listEntry );
        if ( bRootsValid == false || bBonesValid == false )
            return false;
        if ( _listEntry.empty() )
        {
            SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskNeitherRoots", "%#: bone mask has neither 'roots' nor 'bones'", context );
            return false;
        }
        return true;
    }

    bool AnimationBoneMaskDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        _listEntry.clear();
        XMLDocument doc;
        if ( path.empty() || doc.loadResource( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskFileCould", "Bone mask file '%#' could not be read", path );
            return false;
        }
        const XMLNode root = doc.getRoot( "AnimationBoneMaskDef" );
        if ( root.isValid() == false || XMLReadUtil::hasOnlyKnownNames( root, {}, { "roots", "bones" }, path ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskFileMust", "Bone mask file '%#' must be an <AnimationBoneMaskDef> element", path );
            return false;
        }
        if ( parseObject( root, path ) == false )
            return false;
        _filePath = string{ path };
        return true;
    }

    void AnimationBoneMaskDef::writeObject( const XMLNode& element ) const
    {
        for ( uint32 listIndex = 0; listIndex < 2; ++listIndex )
        {
            const uint8 bPropagate = listIndex == 0 ? SW_TRUE : SW_FALSE;
            bool        bAny       = false;
            for ( const AnimationBoneMaskEntry& entry : _listEntry )
            {
                bAny = bAny || entry._bPropagate == bPropagate;
            }
            if ( bAny == false )
                continue;
            const XMLNode listNode = element.appendChild( AnimationBoneMaskInternal::kArrListKey[listIndex] );
            for ( const AnimationBoneMaskEntry& entry : _listEntry )
            {
                if ( entry._bPropagate != bPropagate )
                    continue;
                AnimationBoneMaskEntryParams params;
                params._bone   = entry._bone;
                params._weight = entry._weight;
                XMLFields::writeFields( listNode.appendChild( AnimationBoneMaskInternal::kEntryTag ), params );
            }
        }
    }

    bool AnimationBoneMaskDef::computeBoneWeights( const Skeleton& skeleton, vector<float32>& outListWeight ) const
    {
        const uint32 boneCount = skeleton.getBoneCount();
        outListWeight.assign( boneCount, 0.0f );
        // 본마다 그 본에서 시작하는 root 의 가중치다(-1 은 root 가 아님).
        vector<float32> listRootWeight( boneCount, -1.0f );
        vector<int32>   listSingleBone( _listEntry.size(), constant::invalid_index::kInt32 );
        for ( size_t entryIndex = 0; entryIndex < _listEntry.size(); ++entryIndex )
        {
            const AnimationBoneMaskEntry& entry     = _listEntry[entryIndex];
            const optional<uint32>        boneIndex = skeleton.findBoneIndex( entry._bone );
            if ( boneIndex.has_value() == false )
            {
                SW_LOG_ERROR_ID( "AnimationBoneMask.BoneMaskNamesBone", "Bone mask '%#' names bone '%#' that is not in the skeleton", _name.c_str(), entry._bone.c_str() );
                outListWeight.assign( boneCount, 0.0f );
                return false;
            }
            if ( entry._bPropagate == SW_TRUE )
                listRootWeight[*boneIndex] = entry._weight;
            else
                listSingleBone[entryIndex] = static_cast<int32>( *boneIndex );
        }
        // 부모가 자식보다 앞이므로 한 번 훑으면 root 의 값이 자손에 퍼진다(가까운 root 가 이긴다).
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const int32 parentIndex = skeleton.getBone( boneIndex )._parentIndex;
            if ( listRootWeight[boneIndex] >= 0.0f )
                outListWeight[boneIndex] = listRootWeight[boneIndex];
            else if ( parentIndex >= 0 )
                outListWeight[boneIndex] = outListWeight[static_cast<uint32>( parentIndex )];
        }
        for ( size_t entryIndex = 0; entryIndex < _listEntry.size(); ++entryIndex )
        {
            if ( listSingleBone[entryIndex] >= 0 )
                outListWeight[static_cast<uint32>( listSingleBone[entryIndex] )] = _listEntry[entryIndex]._weight;
        }
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( AnimationBoneMaskDef, AnimationBoneMaskDef::kAssetType );
} // namespace sw
