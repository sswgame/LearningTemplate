/**
 * @file RetargetPreviewModel.h
 * @brief 리타깃 창의 3D 미리보기 상태입니다 — 읽어 둔 두 스켈레톤, 세 자세의 본 위치, 카메라를 다시 맞출 때인지(ImGui 없음).
 * @details 프로필 편집은 스켈레톤을 바꾸지 않는다. 스켈레톤 경로가 바뀌었을 때만 디스크를 읽고 카메라를 맞춘다 —
 *          언리얼 애셋 편집기처럼 값 하나 고쳤다고 사용자가 돌려 둔 뷰를 되돌리지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Animation/Skeletal/Skeleton.h"

namespace sw
{
    class RetargetProfile;
} // namespace sw

namespace sw::editor
{
    /** @class RetargetPreviewModel @brief 파일 머리말을 참고하세요. */
    class RetargetPreviewModel
    {
    public:
        /** @brief 카메라가 담는 구의 여유 배율입니다. */
        static constexpr float32 kFramePadding = 1.15f;
        /** @brief 카메라가 담는 구의 최소 반지름(m)입니다. */
        static constexpr float32 kMinFrameRadius = 0.5f;

        RetargetPreviewModel();

        /** @brief 프로필의 스켈레톤 경로가 읽어 둔 것과 다르면 true 입니다. */
        bool isStale( const RetargetProfile& profile ) const;
        /** @brief 프로필의 두 스켈레톤을 디스크에서 읽고 세 자세를 다시 만듭니다. 못 읽은 쪽은 준비되지 않은 것으로 둡니다. */
        void reloadSkeletons( const RetargetProfile& profile );
        /** @brief 읽어 둔 스켈레톤을 들이고 세 자세를 다시 만듭니다. 없는 쪽은 nullptr 입니다. 다음 그리기에서 카메라를 맞춥니다. */
        void adoptSkeletons( const RetargetProfile& profile, const Skeleton* pSource, const Skeleton* pTarget );
        /** @brief 읽어 둔 스켈레톤으로 세 자세의 본 위치를 다시 만듭니다. 리타깃 포즈를 끄는 동안에도 부릅니다. 없는 본을 적었으면 리타깃 포즈를 얹지 않습니다(이유는 점검 칸이 말한다). */
        void rebuildPoses( const RetargetProfile& profile );

        bool                  isSourceReady() const { return _bSourceReady == SW_TRUE; }
        bool                  isTargetReady() const { return _bTargetReady == SW_TRUE; }
        bool                  isAnyReady() const { return isSourceReady() || isTargetReady(); }
        const Skeleton&       getSourceSkeleton() const { return _source; }
        const Skeleton&       getTargetSkeleton() const { return _target; }
        const vector<float3>& getSourcePositions() const { return _listSourcePosition; }
        const vector<float3>& getTargetReferencePositions() const { return _listTargetReferencePosition; }
        const vector<float3>& getTargetPosePositions() const { return _listTargetPosePosition; }
        /** @brief 원본 스켈레톤에 있는 본이면 true 입니다. 원본을 못 읽었으면 false 입니다. */
        bool hasSourceBone( const hashed_string& name ) const;
        /** @brief 대상 스켈레톤에 있는 본이면 true 입니다. 대상을 못 읽었으면 false 입니다. */
        bool hasTargetBone( const hashed_string& name ) const;

        /** @brief 스켈레톤을 새로 읽은 뒤 처음 부르면 true 이고 표시를 지웁니다. */
        [[nodiscard]] bool consumeFramePending();
        /** @brief 세 자세가 모두 담기는 구입니다. 그을 본이 없으면 false 입니다. */
        [[nodiscard]] bool findFrameSphere( float3& outCenter, float32& outRadius ) const;

    private:
        Skeleton       _source;
        Skeleton       _target;
        string         _sourcePath;
        string         _targetPath;
        vector<float3> _listSourcePosition;
        vector<float3> _listTargetReferencePosition;
        vector<float3> _listTargetPosePosition;
        uint8          _bSourceReady  : 1;
        uint8          _bTargetReady  : 1;
        uint8          _bFramePending : 1; ///< 스켈레톤을 새로 읽었다 — 다음 그리기에서 카메라를 담는다
    };
} // namespace sw::editor
