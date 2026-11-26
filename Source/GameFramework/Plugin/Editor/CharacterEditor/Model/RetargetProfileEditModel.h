/**
 * @file RetargetProfileEditModel.h
 * @brief 리타깃 프로파일 창의 문서 Model 입니다 — 편집 중인 프로필 한 벌과 그 편집 연산. 연산이 성공하면 모델이 정한 되돌리기 이름으로 알린다(감사 E-16). ImGui 를 모릅니다.
 * @details 예전에는 창(View)이 프로필을 직접 고치고 이름을 붙여 알렸다. 이제 창은 읽기만 하고(`getProfile`), 고치는 길은 아래 연산뿐이다 — 언리얼 IK Retargeter 에디터가
 *          컨트롤러를 거쳐 에셋을 고치고 트랜잭션을 여는 자리와 같다. 문서를 통째로 바꾸는 읽기 · 되돌리기(`replace`)는 편집이 아니라 알리지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"

namespace sw::editor
{
    class DocumentEditRecorder;

    /**
     * @class RetargetProfileEditModel
     * @brief 편집 중인 리타깃 프로필 한 벌입니다. 파일 머리말 참고.
     */
    class RetargetProfileEditModel
    {
    public:
        /** @brief 편집 알림 창구를 겁니다 — 문서 패널이 만들 때 한 번 건다(`EditorDocumentPanel::getDocumentEditRecorder`). */
        void bindDocumentEditRecorder( const DocumentEditRecorder& recorder ) { _pEditRecorder = &recorder; }

        const RetargetProfile& getProfile() const { return _profile; }
        /** @brief 문서를 통째로 바꿉니다(읽기 · 되돌리기 · 새 문서). 편집이 아니라 알리지 않는다. */
        void replace( RetargetProfile profile ) { _profile = std::move( profile ); }

        /** @brief 두 스켈레톤 경로를 정하고 "Retarget skeletons" 로 알립니다. */
        void setSkeletonPaths( string_view sourcePath, string_view targetPath );
        /** @brief 루트 · 골반 짝을 정하고(이동 방법은 그대로) "Retarget root and pelvis" 로 알립니다. */
        void setRootAndPelvis( const hashed_string& sourceRoot, const hashed_string& targetRoot, const hashed_string& sourcePelvis, const hashed_string& targetPelvis );
        /** @brief 이동 방법을 정하고 "Retarget translation" 으로 알립니다. */
        void setTranslationMode( RetargetTranslationMode mode );
        /** @brief 같은 이름 자동 짝짓기를 정하고 "Retarget same names" 로 알립니다. */
        void setMapSameNames( bool bMap );
        /** @brief 이동 비율 덮어쓰기를 정하고 "Retarget height ratio" 로 알립니다. */
        void setHeightRatioOverride( float32 ratio );
        /** @brief 사슬 목록을 바꾸고 "Retarget chains" 로 알립니다. */
        void setChains( vector<RetargetChain>&& listChain );
        /** @brief 목표 자세를 바꾸고 "Retarget pose" 로 알립니다. */
        void setTargetPose( vector<RetargetPoseBone>&& listPose );
        /** @brief 본 짝 표를 바꾸고 "Retarget bone map" 으로 알립니다. */
        void setBonePairs( vector<RetargetBonePair>&& listPair );

    private:
        /** @brief 문서를 고쳤음을 알립니다(이 모델의 편집 연산만 부른다 — 감사 E-16). 창구를 걸지 않았으면 멈춘다. */
        void recordDocumentEdit( string_view label ) const;

    private:
        RetargetProfile             _profile{};
        const DocumentEditRecorder* _pEditRecorder{ nullptr }; ///< 편집 알림 창구(`bindDocumentEditRecorder` — 문서 패널이 쥔다)
    };
} // namespace sw::editor
