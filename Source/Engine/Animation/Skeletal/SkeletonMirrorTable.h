/**
 * @file SkeletonMirrorTable.h
 * @brief 스켈레톤 하나의 좌우 본 짝 표(`<이름>.mirrortable.xml`)입니다. 포즈 노드 `Mirror`(`MirrorPoseNode`)가 이 표로 왼쪽 본의 포즈를 오른쪽 본에 옮깁니다.
 * @details 언리얼 `UMirrorDataTable`(본 이름 → 짝 이름 행) 자리다. 스켈레톤 파일은 임포트가 다시 쓰므로 손으로 적는 짝은 본 LOD 처럼 옆 파일에 둔다(`Skeleton::makeSidePath`).
 *          형식: `<SkeletonMirrorTable><_listPair><SkeletonMirrorPair _bone="hand_l" _mirroredBone="hand_r" /></_listPair></SkeletonMirrorTable>`.
 *          짝은 한 줄로 양쪽이다(hand_l ↔ hand_r). 표에 없는 본은 자기 자신과 짝이다(척추 · 머리처럼 대칭면 위의 본).
 *          대칭 축은 이 표가 아니라 노드 설정(`<pose_node type="Mirror" mirror_axis="X">`)이다 — 같은 스켈레톤을 다른 축으로 뒤집는 그래프도 표 하나를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Skeleton;

    /** @brief 좌우 짝 한 줄입니다. 둘 다 스켈레톤에 있어야 하고 서로 달라야 합니다. */
    REFLECT()
    struct SW_API SkeletonMirrorPair
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Bone on one side" )
        hashed_string _bone;
        PROPERTY( Required, Tooltip = "Bone on the other side that takes this bone's mirrored pose (and gives it its own)" )
        hashed_string _mirroredBone;
    };

    /**
     * @class SkeletonMirrorTable
     * @brief 좌우 본 짝 표입니다. 파일 머리말 참고.
     */
    REFLECT()
    class SW_API SkeletonMirrorTable
    {
    public:
        REFLECT_BODY();

        /** @brief 미러 표 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".mirrortable.xml";

        /** @brief XML 본문을 읽습니다. 모르는 키 · 빠진 칸이면 오류를 남기고 false 이며 표는 빕니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 짝 줄입니다. */
        const vector<SkeletonMirrorPair>& getPairs() const { return _listPair; }

        /**
         * @brief 본마다 짝 본 번호(`outListMirroredBone[i]`, 짝이 없으면 i)를 만듭니다. `MirrorPoseNode` 가 평가에 받는 표입니다.
         * @return 스켈레톤에 없는 본 · 자기 자신과 짝 · 두 짝에 든 본이 있으면 오류를 남기고 false 입니다(표는 빕니다).
         */
        [[nodiscard]] bool buildBoneMap( const Skeleton& skeleton, vector<uint32>& outListMirroredBone, string_view sourceLabel ) const;

        /** @brief 스켈레톤 경로 옆의 미러 표 경로입니다(`Skeleton::makeSidePath`). 스켈레톤 확장자가 아니면 빈 문자열입니다. */
        static string makePathForSkeleton( string_view skeletonPath );

    private:
        PROPERTY( Required, Tooltip = "Left-right bone pairs; bones not listed mirror onto themselves" )
        vector<SkeletonMirrorPair> _listPair;
    };
} // namespace sw
