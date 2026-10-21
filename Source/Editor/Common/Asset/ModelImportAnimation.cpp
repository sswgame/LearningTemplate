/**
 * @file ModelImportAnimation.cpp
 * @brief 모델 임포트의 스켈레톤 · 애니메이션 채널 읽기 · 리샘플 · 모프 가중치 곡선 · 클립 곁 데이터(`.clips.json`) 읽기입니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Asset/ModelImportUtil.h"
#include "Editor/Common/Asset/ModelImporter.h"

#include "Engine/Animation/AnimationJSONUtil.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    string ModelImportUtil::makeNodeName( const cgltf_data& data, const cgltf_node& node )
    {
        if ( node.name != nullptr && node.name[0] != '\0' )
            return string( node.name );
        return "node" + to_string( static_cast<uint64>( &node - data.nodes ) );
    }

    string ModelImportUtil::makeFileStem( string_view name )
    {
        string stem;
        stem.reserve( name.size() );
        for ( const utf8 character : name )
        {
            const utf8 lower    = StringUtil::toLowerChar( character );
            const bool bAllowed = ( 'a' <= lower && lower <= 'z' ) || ( '0' <= lower && lower <= '9' ) || lower == '_' || lower == '-' || lower == '.';
            stem += bAllowed ? lower : '_';
        }
        return stem.empty() ? string( "unnamed" ) : stem;
    }

    string ModelImportUtil::makeNumberedStem( const string& baseStem, uint32 useCount )
    {
        string stem = baseStem;
        if ( useCount > 0 )
        {
            stem += "_";
            stem += to_string( useCount ).c_str();
        }
        return stem;
    }

    BoneTransform ModelImportUtil::readNodeLocal( const cgltf_node& node )
    {
        BoneTransform transform{};
        if ( node.has_matrix != 0 )
        {
            float32 arrMatrix[16]{};
            Memory::copy( arrMatrix, node.matrix, sizeof( arrMatrix ) );
            transform = BoneTransform::makeFromMatrix( float4x4{ arrMatrix } );
            return transform;
        }
        if ( node.has_translation != 0 )
            transform._translation = float3{ node.translation[0], node.translation[1], node.translation[2] };
        if ( node.has_rotation != 0 )
            transform._rotation = quaternion{ node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] }.normalize();
        if ( node.has_scale != 0 )
            transform._scale = float3{ node.scale[0], node.scale[1], node.scale[2] };
        return transform;
    }

    const cgltf_node* ModelImportUtil::findJointAncestor( const cgltf_node& node, const unordered_map<const cgltf_node*, int32>& mapJointBone )
    {
        for ( const cgltf_node* pParent = node.parent; pParent != nullptr; pParent = pParent->parent )
        {
            if ( mapJointBone.find( pParent ) != mapJointBone.end() )
                return pParent;
        }
        return nullptr;
    }

    const cgltf_node* ModelImportUtil::findJointAncestorBySlot( const cgltf_node& node, const unordered_map<const cgltf_node*, uint32>& mapJointSlot )
    {
        for ( const cgltf_node* pParent = node.parent; pParent != nullptr; pParent = pParent->parent )
        {
            if ( mapJointSlot.find( pParent ) != mapJointSlot.end() )
                return pParent;
        }
        return nullptr;
    }

    bool ModelImportUtil::readChannel( const cgltf_animation_sampler& sampler, uint32 componentCount, ChannelData& outChannel )
    {
        outChannel                 = ChannelData{};
        outChannel._componentCount = componentCount;
        outChannel._interpolation  = sampler.interpolation;
        if ( sampler.input == nullptr || sampler.output == nullptr )
            return true; // 샘플러가 가리키는 접근자가 없는 채널은 키가 없다(레스트 값을 쓴다)
        // 접근자 하나를 통째로 읽는다 — 키 하나씩 읽다 실패한 키를 0 으로 남기지 않는다.
        return unpackFloats( sampler.input, 1, outChannel._listTime ) && unpackFloats( sampler.output, componentCount, outChannel._listValue );
    }

    bool ModelImportUtil::evaluateChannel( const ChannelData& channel, float32 time, bool bRotation, float32* pOutValue )
    {
        const size_t keyCount = channel._listTime.size();
        const uint32 width    = channel._componentCount;
        if ( keyCount == 0 || width == 0 )
            return false;
        const bool   bCubic      = channel._interpolation == cgltf_interpolation_type_cubic_spline;
        const size_t valueStride = bCubic ? width * 3u : width;
        const size_t valueOffset = bCubic ? width : 0u;
        if ( channel._listValue.size() < keyCount * valueStride )
            return false;

        size_t nextKey = 0;
        while ( nextKey < keyCount && channel._listTime[nextKey] <= time )
        {
            ++nextKey;
        }
        if ( nextKey == 0 || nextKey == keyCount )
        {
            const size_t key = nextKey == 0 ? 0 : keyCount - 1;
            Memory::copy( pOutValue, &channel._listValue[key * valueStride + valueOffset], width * sizeof( float32 ) );
            return true;
        }
        const size_t   previousKey = nextKey - 1;
        const float32  span        = channel._listTime[nextKey] - channel._listTime[previousKey];
        const float32  alpha       = span > 0.0f ? ( time - channel._listTime[previousKey] ) / span : 0.0f;
        const float32* pFrom       = &channel._listValue[previousKey * valueStride + valueOffset];
        const float32* pTo         = &channel._listValue[nextKey * valueStride + valueOffset];
        if ( channel._interpolation == cgltf_interpolation_type_step )
        {
            Memory::copy( pOutValue, pFrom, width * sizeof( float32 ) );
            return true;
        }
        if ( bCubic )
        {
            const float32* pOutTangent = &channel._listValue[previousKey * valueStride + width * 2u];
            const float32* pInTangent  = &channel._listValue[nextKey * valueStride];
            const float32  alpha2      = alpha * alpha;
            const float32  alpha3      = alpha2 * alpha;
            for ( uint32 component = 0; component < width; ++component )
            {
                pOutValue[component] = ( 2.0f * alpha3 - 3.0f * alpha2 + 1.0f ) * pFrom[component] + ( alpha3 - 2.0f * alpha2 + alpha ) * span * pOutTangent[component] +
                                       ( -2.0f * alpha3 + 3.0f * alpha2 ) * pTo[component] + ( alpha3 - alpha2 ) * span * pInTangent[component];
            }
        }
        else if ( bRotation )
        {
            const quaternion rotation = quaternion::slerp( quaternion{ pFrom }, quaternion{ pTo }, alpha );
            pOutValue[0]              = rotation._x;
            pOutValue[1]              = rotation._y;
            pOutValue[2]              = rotation._z;
            pOutValue[3]              = rotation._w;
        }
        else
        {
            for ( uint32 component = 0; component < width; ++component )
            {
                pOutValue[component] = MathUtil::lerp( pFrom[component], pTo[component], alpha );
            }
        }
        return true;
    }

    BoneTransform ModelImportUtil::sampleJoint( const JointChannels& channels, const BoneTransform& rest, float32 time )
    {
        BoneTransform sample = rest;
        float32       arrValue[4]{};
        if ( evaluateChannel( channels._translation, time, false, arrValue ) )
            sample._translation = float3{ arrValue };
        if ( evaluateChannel( channels._rotation, time, true, arrValue ) )
            sample._rotation = quaternion{ arrValue }.normalize();
        if ( evaluateChannel( channels._scale, time, false, arrValue ) )
            sample._scale = float3{ arrValue };
        return sample;
    }

    float32 ModelImportUtil::findLastTime( const ChannelData& channel )
    {
        return channel._listTime.empty() ? 0.0f : channel._listTime.back();
    }

    bool ModelImportUtil::resampleAnimation( const cgltf_animation& animation, const Skeleton& skeleton, const unordered_map<const cgltf_node*, int32>& mapJointBone,
                                             const vector<BoneTransform>& listRest, const vector<float4x4>& listRootParent, float32 sampleRate, float32 minimumDuration,
                                             AnimationRawClip& outRawClip )
    {
        const uint32          boneCount = skeleton.getBoneCount();
        vector<JointChannels> listChannel( boneCount );
        float32               duration = minimumDuration;
        for ( cgltf_size channelIndex = 0; channelIndex < animation.channels_count; ++channelIndex )
        {
            const cgltf_animation_channel& channel = animation.channels[channelIndex];
            const auto                     it      = channel.target_node != nullptr ? mapJointBone.find( channel.target_node ) : mapJointBone.end();
            if ( it == mapJointBone.end() || channel.sampler == nullptr )
                continue; // 관절이 아닌 노드 · 모프 가중치(커브로 따로 싣는다 — readMorphWeightCurves)
            JointChannels& joint = listChannel[static_cast<uint32>( it->second )];
            bool           bRead = true;
            if ( channel.target_path == cgltf_animation_path_type_translation )
                bRead = readChannel( *channel.sampler, 3, joint._translation );
            else if ( channel.target_path == cgltf_animation_path_type_rotation )
                bRead = readChannel( *channel.sampler, 4, joint._rotation );
            else if ( channel.target_path == cgltf_animation_path_type_scale )
                bRead = readChannel( *channel.sampler, 3, joint._scale );
            if ( bRead == false )
                return false;
        }
        for ( const JointChannels& joint : listChannel )
        {
            duration = MathUtil::max( duration, MathUtil::max( findLastTime( joint._translation ), MathUtil::max( findLastTime( joint._rotation ), findLastTime( joint._scale ) ) ) );
        }

        const uint32 intervalCount = duration > 0.0f ? MathUtil::max( 1u, static_cast<uint32>( MathUtil::ceil( duration * sampleRate - 1e-3f ) ) ) : 0u;
        outRawClip                 = AnimationRawClip{};
        outRawClip._sampleCount    = intervalCount + 1;
        outRawClip._sampleRate     = intervalCount > 0 ? static_cast<float32>( intervalCount ) / duration : sampleRate;
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            outRawClip._listTrackName.push_back( skeleton.getBone( boneIndex )._name );
            outRawClip._listTrackParent.push_back( skeleton.getBone( boneIndex )._parentIndex );
        }
        outRawClip._listSample.resize( static_cast<size_t>( outRawClip._sampleCount ) * boneCount );
        for ( uint32 sampleIndex = 0; sampleIndex < outRawClip._sampleCount; ++sampleIndex )
        {
            const float32 time = intervalCount > 0 ? duration * static_cast<float32>( sampleIndex ) / static_cast<float32>( intervalCount ) : 0.0f;
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                BoneTransform sample = convertTransformToEngineSpace( sampleJoint( listChannel[boneIndex], listRest[boneIndex], time ) );
                if ( listRootParent[boneIndex] != float4x4::Identity )
                    sample = BoneTransform::makeFromMatrix( sample.toMatrix() * listRootParent[boneIndex] );
                outRawClip._listSample[static_cast<size_t>( sampleIndex ) * boneCount + boneIndex] = sample;
            }
        }
        return true;
    }

    bool ModelImportUtil::readMorphWeightCurves( const cgltf_animation& animation, float32 sampleRate, vector<AnimationClipCurve>& outListCurve, float32& outDuration )
    {
        outListCurve.clear();
        float32& duration = outDuration;
        duration          = 0.0f;
        for ( cgltf_size channelIndex = 0; channelIndex < animation.channels_count; ++channelIndex )
        {
            const cgltf_animation_channel& channel                = animation.channels[channelIndex];
            const bool                     bNotMorphWeightChannel = channel.target_path != cgltf_animation_path_type_weights || channel.sampler == nullptr || channel.target_node == nullptr ||
                                                channel.target_node->mesh == nullptr || channel.sampler->input == nullptr || channel.sampler->output == nullptr;
            if ( bNotMorphWeightChannel )
                continue;
            const cgltf_mesh& mesh        = *channel.target_node->mesh;
            const cgltf_size  targetCount = mesh.primitives_count > 0 ? mesh.primitives[0].targets_count : 0;
            if ( targetCount == 0 )
                continue;
            ChannelData data{};
            data._componentCount = static_cast<uint32>( targetCount );
            data._interpolation  = channel.sampler->interpolation;
            // 접근자 하나를 통째로 읽는다 — 키 하나씩 읽다 실패한 키를 0 으로 남기지 않는다.
            if ( unpackFloats( channel.sampler->input, 1, data._listTime ) == false || unpackFloats( channel.sampler->output, 1, data._listValue ) == false )
                return false;
            const float32 lastTime = findLastTime( data );
            duration               = MathUtil::max( duration, lastTime );
            // 키 시각(선형 · 계단) 또는 표본율 격자(큐빅)에서 값을 뽑는다.
            vector<float32> listTime = data._listTime;
            if ( data._interpolation == cgltf_interpolation_type_cubic_spline && sampleRate > 0.0f )
            {
                listTime.clear();
                const uint32 intervalCount = MathUtil::max( 1u, static_cast<uint32>( MathUtil::ceil( lastTime * sampleRate ) ) );
                for ( uint32 sampleIndex = 0; sampleIndex <= intervalCount; ++sampleIndex )
                {
                    listTime.push_back( lastTime * static_cast<float32>( sampleIndex ) / static_cast<float32>( intervalCount ) );
                }
            }
            vector<float32> listValue( targetCount );
            for ( cgltf_size targetIndex = 0; targetIndex < targetCount; ++targetIndex )
            {
                const hashed_string name       = makeMorphTargetName( &mesh, targetIndex );
                const bool          bDuplicate = std::any_of( outListCurve.begin(), outListCurve.end(), [&name]( const AnimationClipCurve& curve )
                         { return curve._name == name; } );
                if ( bDuplicate )
                    continue;
                AnimationClipCurve curve{};
                curve._name = name;
                for ( const float32 time : listTime )
                {
                    if ( evaluateChannel( data, time, false, listValue.data() ) )
                        curve._listKey.push_back( AnimationClipCurveKey{ time, listValue[targetIndex] } );
                }
                outListCurve.push_back( std::move( curve ) );
            }
        }
        return true;
    }

    bool ModelImportUtil::readClipData( string_view sourcePath, unordered_map<string, ClipExtra>& outMapExtra )
    {
        outMapExtra.clear();
        const string clipDataPath = ModelImporter::makeClipDataPath( sourcePath );
        if ( FileUtil::exists( clipDataPath ) == false )
            return true;
        JSONDocument document;
        if ( document.loadFile( clipDataPath ) == false ) // 원본 모델 옆 디스크 파일이라 디스크 입구로 읽는다
        {
            SW_LOG_ERROR( "Clip data '%#' is not valid JSON", clipDataPath.c_str() );
            return false;
        }
        const JSONValue root = document.getRoot();
        if ( AnimationJSONUtil::hasOnlyKnownKeys( root, { "clips" }, clipDataPath ) == false || root.get( "clips" ).isObject() == false )
            return false;
        const JSONValue clips = root.get( "clips" );
        for ( const string& clipName : clips.getMemberNames() )
        {
            const JSONValue clipValue = clips.get( clipName, false );
            if ( AnimationJSONUtil::hasOnlyKnownKeys( clipValue, { "loop", "notifies", "curves" }, clipDataPath ) == false )
                return false;
            ClipExtra extra{};
            if ( clipValue.has( "loop" ) )
            {
                const std::optional<bool> bLoop = clipValue.readRequired<bool>( "loop" );
                if ( bLoop.has_value() == false )
                    return false;
                extra._loopOverride = *bLoop ? 1 : 0;
            }
            const JSONValue notifies = clipValue.get( "notifies" );
            for ( size_t notifyIndex = 0; notifies.isArray() && notifyIndex < notifies.size(); ++notifyIndex )
            {
                const JSONValue notify         = notifies.at( notifyIndex );
                const bool      bInvalidNotify = AnimationJSONUtil::hasOnlyKnownKeys( notify, { "name", "time", "duration" }, clipDataPath ) == false || notify.get( "name" ).isString() == false ||
                                            notify.get( "time" ).isNumber() == false;
                if ( bInvalidNotify )
                {
                    SW_LOG_ERROR( "Clip data '%#': notify %# of '%#' needs a name and a time", clipDataPath.c_str(), notifyIndex, clipName.c_str() );
                    return false;
                }
                AnimationNotifyEvent event{};
                event._name                       = hashed_string( notify.get( "name" ).asString() );
                const std::optional<float32> time = notify.readRequired<float32>( "time" );
                // 길이는 없으면 형식의 초기값(순간 알림)이다.
                if ( time.has_value() == false || notify.readOptionalInto( "duration", event._duration ) == false )
                    return false;
                event._time = *time;
                extra._listNotify.push_back( event );
            }
            const JSONValue curves = clipValue.get( "curves" );
            for ( const string& curveName : curves.isObject() ? curves.getMemberNames() : vector<string>{} )
            {
                const JSONValue    keys = curves.get( curveName, false );
                AnimationClipCurve curve{};
                curve._name = hashed_string( curveName );
                for ( size_t keyIndex = 0; keys.isArray() && keyIndex < keys.size(); ++keyIndex )
                {
                    float32 arrKey[2]{};
                    if ( AnimationJSONUtil::readFloats( keys.at( keyIndex ), arrKey, 2 ) == false )
                    {
                        SW_LOG_ERROR( "Clip data '%#': curve '%#' keys must be [time, value] pairs", clipDataPath.c_str(), curveName.c_str() );
                        return false;
                    }
                    curve._listKey.push_back( AnimationClipCurveKey{ arrKey[0], arrKey[1] } );
                }
                extra._listCurve.push_back( std::move( curve ) );
            }
            outMapExtra.emplace( clipName, std::move( extra ) );
        }
        return true;
    }
} // namespace sw::editor
