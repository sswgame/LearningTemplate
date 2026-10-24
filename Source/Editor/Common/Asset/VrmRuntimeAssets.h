/**
 * @file VrmRuntimeAssets.h
 * @brief VRM 아바타 정보(`VrmAvatarDef`)를 런타임이 이미 읽는 형식으로 옮깁니다 — 표정은 얼굴 리그(`<이름>.facial.xml`, `FacialRig`),
 *        스프링 본은 후처리 리그(`<이름>.rig.xml`, `RigAsset` 의 `SpringChain` 노드)입니다.
 * @details 런타임에 VRM 전용 타입을 두지 않는다 — 같은 일을 하는 엔진 타입(얼굴 리그의 표정 · 깜빡임 · 비즘, 리그의 스프링 사슬 · 구와 캡슐 충돌체)이 있으므로
 *          임포트가 그 데이터를 만든다. 캐릭터는 그 파일을 다른 얼굴 리그 · 리그처럼 참조한다(`PoseModifierComponent` 등).
 *          옮기지 못하는 값은 @p pOutListIgnored 에 이름을 모은다(호출하는 쪽이 경고로 알린다):
 *          - 표정의 override(blink · lookAt · mouth 누르기)와 isBinary — 얼굴 리그에 그 규칙이 없다.
 *          - 깜빡임 표정의 가중치 — 얼굴 리그의 깜빡임 타깃은 이름뿐(가중치 1)이다.
 *          - 스프링의 center 본 — 리그 사슬은 월드 기준이다.
 *          스프링 물성은 three-vrm `VRMSpringBoneJoint.update`(VRM 1.0 스펙의 참조 구현)와 항마다 같다: dragForce = 감쇠, stiffness = `axis_pull`(애니메이션 방향으로 m/s),
 *          gravityPower × gravityDir = 스텝마다 위치 더하기라 가속도로는 ÷ 스텝, hitRadius = 입자 반지름 — 관절마다 값은 리그의 `joints` 로 간다.
 *          남는 차이: 리그는 고정 스텝(1/60 초)이고 VRM 은 프레임 시간이다. 뼈 방향은 애니메이션 자세의 부모 방향이다(VRM 은 시뮬레이션된 부모 × 처음 로컬 회전).
 *          0.x 의 끝 본(자식 없음)은 VRM 이 가상 꼬리(7 cm)로 돌리지만 리그 사슬은 끝 본을 돌리지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class Skeleton;
} // namespace sw

namespace sw::editor
{
    struct VrmAvatarDef;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct VrmRuntimeAssets
     * @brief VRM 아바타 정보에서 얼굴 리그 · 후처리 리그 XML 을 만듭니다. 모든 함수가 static 입니다.
     */
    struct VrmRuntimeAssets
    {
        /** @brief 비즘으로 옮기는 VRM 프리셋과 립싱크 표(`engine/animation/lipsync.xml`)의 비즘 이름입니다. 나머지 입 모양(ih · oh)은 표정으로 남습니다. */
        struct VisemeMapping
        {
            string_view _vrmPreset;
            string_view _viseme;
        };
        static constexpr VisemeMapping kArrVisemeMapping[] = {
            {"aa", "AA"},
            {"ee", "EE"},
            {"ou", "OO"},
        };
        /** @brief 립싱크 표의 무음 비즘입니다(타깃 없음). */
        static constexpr string_view kSilenceViseme = "sil";

        /** @brief 표정이 하나라도 있으면 얼굴 리그가 있다 — 없으면 파일을 쓰지 않습니다. */
        static bool hasFacialRig( const VrmAvatarDef& def );
        /**
         * @brief 얼굴 리그 XML(`FacialRig` 의 리플렉션 형식)을 만듭니다. 모든 VRM 표정이 같은 이름의 표정이고, aa · ee · ou 는 비즘으로도, blink 는 깜빡임으로도 들어갑니다.
         * @details 깜빡임 타깃은 blink 표정의 타깃, 없으면 blinkLeft · blinkRight 의 타깃을 모은다. 시선(눈 본)은 넣지 않는다(VRM 시선은 옮기지 않는다).
         */
        static string makeFacialRigXML( const VrmAvatarDef& def, vector<string>* pOutListIgnored );

        /** @brief 두 본 이상의 스프링 사슬이 하나라도 나오면 리그가 있다 — 없으면 파일을 쓰지 않습니다. */
        static bool hasSpringRig( const VrmAvatarDef& def, const Skeleton& skeleton );
        /**
         * @brief 후처리 리그 XML(`RigAsset` 형식, `SpringChain` 노드만)을 만듭니다.
         * @details 사슬은 스켈레톤 부모를 따라 끊는다 — 0.x 는 루트 본 아래 자손 전부를 깊이 우선 순서로 담아 가지가 있으면 한 줄이 아니다. 관절의 부모가 앞 관절이
         *          아니면 그 부모에서 새 사슬을 시작한다(부모 본이 고정된 루트다). 두 본 미만의 사슬과 스켈레톤에 없는 본은 넣지 않고 무시 목록에 남긴다.
         *          충돌체는 그 스프링의 충돌체 묶음 전부다(구는 offset, 캡슐은 offset → tail).
         */
        static string makeSpringRigXML( const VrmAvatarDef& def, const Skeleton& skeleton, vector<string>* pOutListIgnored );
    };
} // namespace sw::editor
