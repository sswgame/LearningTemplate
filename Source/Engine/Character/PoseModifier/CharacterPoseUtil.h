/**
 * @file CharacterPoseUtil.h
 * @brief 애니메이션 쪽(스켈레톤, 유닛의 포즈 버퍼)을 캐릭터 형상의 본 배열(`CharacterBoneArray`)로 옮기는 다리입니다.
 * @details 소켓, 체형, 피팅은 메시와 포즈 버퍼를 모르는 값 타입(`CharacterBoneArray`) 위에서 돈다. 외형을 조립하는 쪽은 바인드 본(레퍼런스 포즈)을
 *          `makeBindBones` 로 한 번 만들고, 본이 움직인 프레임마다 `copyUnitPose` 로 유닛의 모델 공간 행렬을 옮겨 소켓 변환을 구한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    struct CharacterBoneArray;

    class SkeletalMeshComponent;
    class Skeleton;

    /** @brief 스켈레톤과 유닛 포즈에서 본 배열을 만드는 도우미입니다(전부 static). */
    struct SW_API CharacterPoseUtil
    {
        /** @brief 스켈레톤의 레퍼런스(바인드) 포즈로 본 배열을 채웁니다(이름, 부모, 로컬, 모델. 기존 내용은 비웁니다). */
        static void makeBindBones( const Skeleton& skeleton, CharacterBoneArray& outBones );
        /**
         * @brief 유닛의 현재 모델 공간 행렬을 본 배열의 모델 슬롯에 옮깁니다. 본 배열이 그 유닛의 스켈레톤 형태가 아니면 바인드 본으로 다시 만들고 옮깁니다.
         * @return 유닛이 아직 포즈를 만들지 않아 옮길 행렬이 없으면 false 입니다(본 배열은 바인드 포즈).
         */
        [[nodiscard]] static bool copyUnitPose( const SkeletalMeshComponent& unit, CharacterBoneArray& inoutBones );
    };
} // namespace sw
