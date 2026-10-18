#include "pch.h"

#include "Engine/Animation/Graph/AnimationBoneMask.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationBoneMask" );

    namespace
    {
        struct AnimationBoneMaskInternal
        {
            /** @brief 항목 배열 이름입니다. 순서는 `_bPropagate` 의 참(roots), 거짓(bones)입니다. */
            static constexpr const utf8* kArrListKey[] = { "roots", "bones" };
            /** @brief 항목 객체가 가질 수 있는 키입니다. */
            static constexpr const utf8* kArrEntryKey[] = { "bone", "weight" };

            /** @brief 배열 하나의 항목을 읽어 덧붙입니다. 잘못되었으면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool parseEntries( const JSONValue& object, const utf8* pListKey, bool bPropagate, string_view context,
                                                    vector<AnimationBoneMaskEntry>& inoutListEntry )
            {
                const JSONValue listJSON = object.get( pListKey );
                if ( listJSON.isValid() == false )
                    return true;
                if ( listJSON.isArray() == false )
                {
                    SW_LOG_ERROR( "%#: bone mask '%#' must be an array", context, pListKey );
                    return false;
                }
                bool bValid = true;
                for ( size_t index = 0; index < listJSON.size(); ++index )
                {
                    const JSONValue entryJSON = listJSON.at( index );
                    if ( entryJSON.isObject() == false )
                    {
                        SW_LOG_ERROR( "%#: bone mask '%#' item %# is not an object", context, pListKey, index );
                        bValid = false;
                        continue;
                    }
                    for ( const string& key : entryJSON.getMemberNames() )
                    {
                        const bool bKnown = key == kArrEntryKey[0] || key == kArrEntryKey[1];
                        if ( bKnown == false )
                        {
                            SW_LOG_ERROR( "%#: bone mask '%#' item %# has unknown key '%#'", context, pListKey, index, key.c_str() );
                            bValid = false;
                        }
                    }
                    AnimationBoneMaskEntry entry{};
                    entry._bone                = hashed_string( entryJSON.get( "bone" ).asString() );
                    entry._bPropagate          = bPropagate ? SW_TRUE : SW_FALSE;
                    const JSONValue weightJSON = entryJSON.get( "weight" );
                    if ( entry._bone.empty() )
                    {
                        SW_LOG_ERROR( "%#: bone mask '%#' item %# has no 'bone'", context, pListKey, index );
                        bValid = false;
                        continue;
                    }
                    if ( weightJSON.isNumber() == false )
                    {
                        SW_LOG_ERROR( "%#: bone mask entry '%#' needs a numeric 'weight'", context, entry._bone.c_str() );
                        bValid = false;
                        continue;
                    }
                    entry._weight           = static_cast<float32>( weightJSON.asFloat() );
                    const bool bWeightValid = 0.0f <= entry._weight && entry._weight <= 1.0f;
                    if ( bWeightValid == false )
                    {
                        SW_LOG_ERROR( "%#: bone mask entry '%#' weight %# is outside [0, 1]", context, entry._bone.c_str(), entry._weight );
                        bValid = false;
                        continue;
                    }
                    inoutListEntry.push_back( entry );
                }
                return bValid;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AnimationBoneMaskDef::parseObject( const JSONValue& object, string_view context )
    {
        _listEntry.clear();
        if ( object.isObject() == false )
        {
            SW_LOG_ERROR( "%#: bone mask must be an object", context );
            return false;
        }
        const bool bRootsValid = AnimationBoneMaskInternal::parseEntries( object, AnimationBoneMaskInternal::kArrListKey[0], true, context, _listEntry );
        const bool bBonesValid = AnimationBoneMaskInternal::parseEntries( object, AnimationBoneMaskInternal::kArrListKey[1], false, context, _listEntry );
        if ( bRootsValid == false || bBonesValid == false )
            return false;
        if ( _listEntry.empty() )
        {
            SW_LOG_ERROR( "%#: bone mask has neither 'roots' nor 'bones'", context );
            return false;
        }
        return true;
    }

    bool AnimationBoneMaskDef::loadFromFile( string_view path )
    {
        _listEntry.clear();
        JSONDocument doc;
        if ( path.empty() || doc.loadResource( path ) == false )
        {
            SW_LOG_ERROR( "Bone mask file '%#' could not be read", path );
            return false;
        }
        if ( parseObject( doc.getRoot(), path ) == false )
            return false;
        _filePath = string{ path };
        return true;
    }

    void AnimationBoneMaskDef::writeObject( const JSONValue& object ) const
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
            const JSONValue listJSON = object.set( AnimationBoneMaskInternal::kArrListKey[listIndex] );
            listJSON.setArray();
            for ( const AnimationBoneMaskEntry& entry : _listEntry )
            {
                if ( entry._bPropagate != bPropagate )
                    continue;
                const JSONValue entryJSON = listJSON.pushBack();
                entryJSON.setObject();
                entryJSON.set( "bone" ).setString( entry._bone.c_str() );
                entryJSON.set( "weight" ).setFloat( static_cast<float64>( entry._weight ) );
            }
        }
    }

    bool AnimationBoneMaskDef::computeBoneWeights( const Skeleton& skeleton, vector<float32>& outListWeight ) const
    {
        const uint32 boneCount = skeleton.getBoneCount();
        outListWeight.assign( boneCount, 0.0f );
        // 본마다 그 본에서 시작하는 root 의 가중치다(-1 은 root 가 아님).
        vector<float32> listRootWeight( boneCount, -1.0f );
        vector<int32>   listSingleBone( _listEntry.size(), -1 );
        for ( size_t entryIndex = 0; entryIndex < _listEntry.size(); ++entryIndex )
        {
            const AnimationBoneMaskEntry& entry     = _listEntry[entryIndex];
            const int32                   boneIndex = skeleton.findBoneIndex( entry._bone );
            if ( boneIndex < 0 )
            {
                SW_LOG_ERROR( "Bone mask '%#' names bone '%#' that is not in the skeleton", _name.c_str(), entry._bone.c_str() );
                outListWeight.assign( boneCount, 0.0f );
                return false;
            }
            if ( entry._bPropagate == SW_TRUE )
                listRootWeight[static_cast<uint32>( boneIndex )] = entry._weight;
            else
                listSingleBone[entryIndex] = boneIndex;
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
