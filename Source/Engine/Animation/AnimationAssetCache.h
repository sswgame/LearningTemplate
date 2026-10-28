/**
 * @file AnimationAssetCache.h
 * @brief 스켈레톤(`.skeleton.xml`), 애니메이션 클립(`.animclip`), 본 LOD(`.bonelod.xml`), 후처리 리그(`.rig.xml`)를 경로별로 나눠 주는 캐시입니다.
 * @details 구현은 `SharedAssetCache` 하나이고, 이 파일은 종류마다 인스턴스화 선언만 둡니다(쓰는 쪽은 `SharedAssetCache<Skeleton>` 처럼 템플릿 이름으로 부릅니다)(종류 이름 · 읽는 함수는 .cpp 의 특수화).
 *          표는 프로세스에 하나(Engine.dll 안)이고 약한 참조라 마지막 사용자가 놓으면 사라진다. 다시 읽기는 넷 모두 제자리에서 바꾼다.
 *          Animation(티어 2)이 아니라 Resource(티어 4)에 있는 것은 `IAssetCache` 를 구현하기 때문이다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"

namespace sw
{
    class AnimationClip;
    class RigAsset;
    class Skeleton;
    class SkeletonBoneLOD;

    /** @brief 스켈레톤을 경로별로 나눠 줍니다("Skeleton"). 같은 경로는 같은 객체라서 클립에서 본으로 가는 테이블을 스켈레톤 포인터로 캐시할 수 있습니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<Skeleton>;
    /** @brief 애니메이션 클립을 경로로 나눠 줍니다("AnimationClip"). 같은 클립을 쓰는 캐릭터 백 명이 파일을 백 번 읽지 않습니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<AnimationClip>;
    /** @brief 스켈레톤 옆의 본 LOD 테이블을 나눠 줍니다("SkeletonBoneLOD"). 유닛은 `SkeletonBoneLOD::getRevision` 으로 다시 읽힌 것을 알아챕니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<SkeletonBoneLOD>;
    /**
     * @brief 후처리 리그를 나눠 줍니다("Rig"). 다시 읽으면 내용 번호(`RigAsset::getContentID`)가 바뀌므로, 쓰는 `PoseModifierComponent` 가 다음 프레임에
     *        다시 바인딩합니다.
     */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<RigAsset>;
} // namespace sw
