/**
 * @file AnimationGraphParams.h
 * @brief 애니메이션 그래프 파일(`*.animgraph.xml`)의 원소마다 적을 수 있는 칸의 스키마입니다(`node` · `blend_space` · `sample` · `pose_node` · `link` · `condition` · `layer` · `comment` · `bone_mask` · 본 마스크의 `entry`).
 * @details 속성 이름 · 필수 · 범위 · 기본값은 여기 `PROPERTY` 가 정본이고, 읽기 · 쓰기는 카탈로그 · 행동 트리와 같은 `XMLFields::readFields` · `writeFields` 다
 *          (속성 이름은 칸 이름에서 앞의 `_` 와 불리언 `b` 를 뗀 것, 다르면 `Name =`). 열거 값은 열거자 이름 그대로다(`op="GreaterEqual"`).
 *          칸 사이의 의미(블렌드 스페이스와 포즈 노드를 함께 적음, 레이어가 없는 마스크를 가리킴, inertialization 의 반감기)는 `AnimationGraphAsset` 이 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief `<node>` 입니다. `loop` 는 적지 않음 · 참 · 거짓 셋이라 칸이 아니라 손으로 읽는다(`AnimationGraphNode::_loop`). */
    REFLECT()
    struct SW_API AnimationGraphNodeParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1 )
        int32 _id{ 0 };
        PROPERTY()
        string _name{};
        PROPERTY()
        float32 _x{ 40.0f };
        PROPERTY()
        float32 _y{ 40.0f };
        PROPERTY( Name = "sync_group" )
        hashed_string _syncGroup{};
        /** @brief 블렌드 스페이스 상태가 모든 표본을 섞는 가장 높은 LOD 등급입니다. 넘으면 가장 무거운 표본 하나로 접는다(-1 = 항상 섞는다, 블렌드 스페이스 상태만). */
        PROPERTY( Name = "max_lod", Min = -1 )
        int32 _maxLod{ -1 };
    };

    /** @brief `<blend_space>` 입니다. `y` 를 적지 않으면 1D 다. */
    REFLECT()
    struct SW_API AnimationGraphBlendSpaceParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _x{};
        PROPERTY()
        hashed_string _y{};
        PROPERTY()
        AnimationBlendSpaceNotifyMode _notify{ AnimationBlendSpaceNotifyMode::All };
    };

    /** @brief `<blend_space><samples><sample>` 입니다. 적지 않은 축은 0 이다. */
    REFLECT()
    struct SW_API AnimationGraphSampleParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _clip{};
        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f };
    };

    /** @brief `<pose_node>` 입니다. 입력은 자식 `<inputs><item>` 이다. */
    REFLECT()
    struct SW_API AnimationGraphPoseNodeParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        AnimationPoseNodeKind _type{ AnimationPoseNodeKind::None };
        PROPERTY()
        hashed_string _weight{};
    };

    /** @brief `<link>` 입니다. 조건은 자식 `<condition>` 이다(없으면 "끝나면 다음"). */
    REFLECT()
    struct SW_API AnimationGraphLinkParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1 )
        int32 _id{ 0 };
        PROPERTY( Required )
        int32 _from{ 0 };
        PROPERTY( Required )
        int32 _to{ 0 };
        /** @brief 크로스페이드 길이입니다. 음수면 플레이어 기본값입니다. */
        PROPERTY( Units = s )
        float32 _blend{ -1.0f };
        PROPERTY( Name = "blend_mode" )
        AnimationBlendMode _blendMode{ AnimationBlendMode::Crossfade };
        PROPERTY( Name = "half_life", Min = 0.0, Units = s )
        float32 _halfLife{ 0.0f };
    };

    /** @brief `<link><condition>` 입니다. */
    REFLECT()
    struct SW_API AnimationGraphConditionParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _param{};
        PROPERTY( Required )
        AnimationConditionOp _op{ AnimationConditionOp::None };
        PROPERTY()
        float32 _value{ 0.0f };
    };

    /** @brief `<layers><layer>` 입니다. */
    REFLECT()
    struct SW_API AnimationGraphLayerParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        /** @brief 이 레이어가 재생할 클립 이름입니다. `slot` 과 둘 중 하나만 적는다. */
        PROPERTY()
        hashed_string _clip{};
        /** @brief 쓰는 쪽이 다른 그래프로 채우는 연결 레이어 슬롯 이름입니다(`SkeletalAnimatorComponent::_listLinkedLayer`). 비어 있는 슬롯은 아무것도 얹지 않는다. */
        PROPERTY()
        hashed_string _slot{};
        /** @brief 비었으면 모든 본이다. */
        PROPERTY()
        hashed_string _mask{};
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _weight{ 1.0f };
        PROPERTY( Required )
        AnimationLayerBlend _blend{ AnimationLayerBlend::Override };
        PROPERTY()
        bool _bLoop{ SW_TRUE };
        PROPERTY( Name = "weight_param" )
        hashed_string _weightParam{};
        /** @brief 다시 시작할 때 끊긴 포즈에서 이어 가는 반감기입니다. 0 이면 즉시 새 포즈다. */
        PROPERTY( Name = "half_life", Min = 0.0, Units = s )
        float32 _halfLife{ 0.0f };
        /** @brief 이 레이어를 얹는 가장 높은 LOD 등급입니다. 넘으면 가중치를 페이드로 내려 끈다(-1 = 항상 얹는다). */
        PROPERTY( Name = "max_lod", Min = -1 )
        int32 _maxLod{ -1 };
    };

    /** @brief `<comments><comment>` 입니다(`GraphCommentBox` 의 파일 모양 — 자리와 크기를 칸 넷으로 적는다). */
    REFLECT()
    struct SW_API AnimationGraphCommentParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 1 )
        int32 _id{ 0 };
        PROPERTY()
        string _text{};
        PROPERTY()
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f };
        PROPERTY()
        float32 _w{ 320.0f };
        PROPERTY()
        float32 _h{ 200.0f };
    };

    /** @brief `<bone_masks><bone_mask>` 입니다. 본문은 프리셋 파일(`file`)이나 바로 적은 자식(`<roots>` · `<bones>` — `AnimationBoneMaskDef::parseObject`) 중 하나다. */
    REFLECT()
    struct SW_API AnimationGraphBoneMaskParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY( AssetPath )
        string _file{};
    };

    /** @brief 본 마스크의 `<roots>` · `<bones>` 아래 `<entry>` 입니다(그래프 안에 적은 마스크와 프리셋 파일 `*.bonemask.xml` 이 같다). */
    REFLECT()
    struct SW_API AnimationBoneMaskEntryParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _bone{};
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _weight{ 0.0f };
    };
} // namespace sw
