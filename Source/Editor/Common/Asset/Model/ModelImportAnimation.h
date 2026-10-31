/**
 * @file ModelImportAnimation.h
 * @brief 스켈레톤 노드, 애니메이션 채널 읽기 · 평가, 균일 리샘플, 모프 가중치 곡선, 클립 부가 데이터(`<y>.clips.json`)를 읽습니다.
 * @note 임포터 내부 전용입니다. 공개 API 는 `ModelImporter.h` 입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Skeletal/Pose.h"

#include <cgltf.h>

namespace sw
{
    struct AnimationRawClip;

    class Skeleton;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct ModelImportAnimation
     * @brief 스켈레톤 노드, 애니메이션 채널 읽기 · 평가, 균일 리샘플, 모프 가중치 곡선, 클립 부가 데이터(`<y>.clips.json`)를 읽습니다.
     */
    struct ModelImportAnimation
    {
        /** @brief 애니메이션 채널 하나를 통째로 읽은 것입니다. 키 시각, 값, 보간 방식을 담습니다. */
        struct ChannelData
        {
            vector<float32>          _listTime;
            vector<float32>          _listValue;
            uint32                   _componentCount{ 0 };
            cgltf_interpolation_type _interpolation{ cgltf_interpolation_type_linear };
        };

        /** @brief 한 관절의 이동, 회전, 스케일 채널입니다. 채널이 없으면 비어 있습니다. */
        struct JointChannels
        {
            ChannelData _translation;
            ChannelData _rotation;
            ChannelData _scale;
        };

        /** @brief 부가 데이터(`<y>.clips.json`)에 적힌 클립 하나의 설정입니다. */
        struct ClipExtra
        {
            vector<AnimationNotifyEvent> _listNotify;
            vector<AnimationClipCurve>   _listCurve;
            int8                         _loopOverride{ -1 };
        };

        /** @brief 노드의 로컬 TRS 를 glTF 공간으로 읽습니다. 행렬로 적힌 노드는 분해합니다. */
        static BoneTransform readNodeLocal( const cgltf_node& node );

        /** @brief 노드에서 가장 가까운 조상 중 관절 집합에 속한 것을 찾습니다(자기 자신은 제외). 없으면 nullptr 입니다. */
        static const cgltf_node* findJointAncestor( const cgltf_node& node, const unordered_map<const cgltf_node*, int32>& mapJointBone );

        /** @brief 노드에서 가장 가까운 관절 조상을 관절 슬롯 테이블 기준으로 찾습니다(자기 자신은 제외). */
        static const cgltf_node* findJointAncestorBySlot( const cgltf_node& node, const unordered_map<const cgltf_node*, uint32>& mapJointSlot );

        /** @brief 채널 하나의 키 시각과 값을 접근자 단위로 통째로 읽습니다. 접근자를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readChannel( const cgltf_animation_sampler& sampler, uint32 componentCount, ChannelData& outChannel );

        /**
         * @brief 채널을 @p time 에서 평가해 값을 구합니다. glTF 보간을 따르며, LINEAR 는 회전이면 slerp, STEP 은 계단, CUBICSPLINE 은 에르미트입니다. 키가 없으면 false 입니다.
         * @details CUBICSPLINE 의 출력은 키마다 (들어오는 접선, 값, 나가는 접선) 셋이다. 접선에 키 간격을 곱해 에르미트 기저로 섞는다.
         */
        static bool evaluateChannel( const ChannelData& channel, float32 time, bool bRotation, float32* pOutValue );

        /** @brief 관절 하나를 @p time 에서 샘플링합니다(glTF 공간). 채널이 없는 성분은 노드의 레스트 값을 씁니다. */
        static BoneTransform sampleJoint( const JointChannels& channels, const BoneTransform& rest, float32 time );

        /** @brief 채널의 마지막 키 시각을 반환합니다. */
        static float32 findLastTime( const ChannelData& channel );

        /**
         * @brief glTF 애니메이션 하나를 관절마다 균일한 간격으로 다시 샘플링합니다. 샘플 레이트는 길이를 정확히 나누도록 조금 조정합니다(마지막 샘플이 끝 시각이 된다).
         * @return 채널 접근자를 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool resampleAnimation( const cgltf_animation& animation, const Skeleton& skeleton, const unordered_map<const cgltf_node*, int32>& mapJointBone,
                                                     const vector<BoneTransform>& listRest, const vector<float4x4>& listRootParent, float32 sampleRate, float32 minimumDuration,
                                                     AnimationRawClip& outRawClip );

        /**
         * @brief 애니메이션의 모프 가중치 채널(`weights`)을 타깃 이름의 커브로 읽습니다. 커브 이름은 모프 타깃 이름이며, 런타임이 이름으로 유닛의 모프 가중치에 연결합니다.
         * @details 출력은 키마다 타깃 수만큼의 스칼라이다(CUBICSPLINE 은 접선, 값, 접선 셋). 선형과 계단은 키 시각 그대로 읽고, 큐빅은 샘플 레이트로 다시 샘플링한다.
         * @param outDuration 가중치 채널의 마지막 키 시각입니다. 클립 길이의 하한이 됩니다.
         * @return 채널 접근자를 읽지 못하면 false 입니다.
         */
        [[nodiscard]] static bool readMorphWeightCurves( const cgltf_animation& animation, float32 sampleRate, vector<AnimationClipCurve>& outListCurve, float32& outDuration );

        /** @brief 부가 데이터(`<y>.clips.json`)를 읽습니다. 파일이 없으면 빈 맵으로 true 를 반환하고, 모르는 키나 잘못된 값이 있으면 false 를 반환합니다. */
        [[nodiscard]] static bool readClipData( string_view sourcePath, unordered_map<string, ClipExtra>& outMapExtra );
    };
} // namespace sw::editor
