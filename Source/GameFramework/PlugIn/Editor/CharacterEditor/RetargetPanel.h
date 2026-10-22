/**
 * @file RetargetPanel.h
 * @brief 리타깃 프로필(`*.retarget.xml`) 편집 창입니다 — 두 스켈레톤 · 뿌리와 골반 짝 · 사슬 쌍(본 목록 · IK 목표) · 본 이름 테이블 · 이동 방법을 고치고,
 *        런타임 리타기터로 묶어 본 점검(사슬 길이 비 · 짝지은 본 수)을 보이며 저장합니다(`docs/plans/AnimationAdvancedFeatures.md` A4, 언리얼 IK Retargeter 창 자리).
 * @details 데이터는 `RetargetProfile` 하나이고 쓰기는 `RetargetProfile::toJSON`, 점검은 `RetargetProfileInspector::inspect`(런타임과 같은 묶기) 하나다 —
 *          에디터 쪽 사본 규칙을 두지 않는다. 묶이지 않는 프로필은 저장하지 않는다. 되돌리기는 문서 패널의 글 스냅샷(`captureDocumentText`)이 한다.
 *          3D 미리보기(두 스켈레톤을 겹쳐 그리기)와 기준 포즈(리타깃 포즈) 편집은 아직 없다 — 계획 문서 A4 의 남은 것.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"

namespace sw::editor
{
    /**
     * @class RetargetPanel
     * @brief 리타깃 프로필 편집 창입니다. 파일 머리말 참고.
     */
    class RetargetPanel final : public EditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "charactereditor.retarget";

        RetargetPanel();

        const utf8*        getPanelTitle() const override { return "Retarget Profile"; }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

        const RetargetProfile&           getProfile() const { return _profile; }
        const RetargetProfileInspection& getInspection() const { return _inspection; }
        /** @brief 지난 점검이 실패한 까닭입니다. 묶였으면 비어 있습니다. */
        const string& getInspectionError() const { return _inspectionError; }

    private:
        /** @brief 사슬 한 줄의 편집 글(쉼표로 나눈 본 이름)입니다. 프로필과 같은 순서입니다. */
        struct ChainText
        {
            fixed_string<constant::kMaxBuffer512> _source{};
            fixed_string<constant::kMaxBuffer512> _target{};
        };

        bool                acceptsDocumentPath( string_view assetPath ) const override;
        ToolAssetLoadResult loadDocument() override;
        string              captureDocumentText() const override { return _profile.toXML(); }
        void                applyDocumentText( string_view text ) override;

        void drawSkeletonRows();
        void drawRootAndPelvisRows();
        void drawOptionRows();
        void drawChainRows();
        void drawBonePairRows();
        void drawInspection();
        /** @brief 편집 하나를 되돌리기에 남기고 다시 점검합니다. */
        void markEdited( const utf8* pLabel );
        /** @brief 프로필에서 편집 글(스켈레톤 경로 · 사슬 본 목록)을 다시 채우고 점검합니다. */
        void syncFromProfile();

        RetargetProfile                      _profile;
        RetargetProfileInspection            _inspection;
        vector<ChainText>                    _listChainText;
        string                               _inspectionError;
        string                               _status;
        fixed_string<constant::kMaxPathSize> _sourceSkeleton;
        fixed_string<constant::kMaxPathSize> _targetSkeleton;
        fixed_string<constant::kMaxBuffer64> _arrPairName[4]; ///< 뿌리 원본 · 뿌리 대상 · 골반 원본 · 골반 대상
        fixed_string<constant::kMaxBuffer64> _newChainName;
        fixed_string<constant::kMaxBuffer64> _newPairSource;
        fixed_string<constant::kMaxBuffer64> _newPairTarget;
    };
} // namespace sw::editor
