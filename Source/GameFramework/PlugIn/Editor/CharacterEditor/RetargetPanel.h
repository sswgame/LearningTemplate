/**
 * @file RetargetPanel.h
 * @brief 리타깃 프로필(`*.retarget.xml`) 편집 창입니다 — 두 스켈레톤 · 루트와 골반 짝 · 사슬 쌍(본 목록 · IK 목표) · 본 이름 테이블 · 이동 방법 · 리타깃 포즈를 고치고,
 *        런타임 리타기터로 묶어 본 점검(사슬 길이 비 · 짝지은 본 수)을 보이며 저장합니다.
 * @details 데이터는 `RetargetProfile` 하나이고 쓰기는 `RetargetProfile::toXML`, 점검은 `RetargetProfileInspector::inspect`(런타임과 같은 묶기) 하나다 —
 *          에디터 쪽 사본 규칙을 두지 않는다. 묶이지 않는 프로필은 저장하지 않는다. 되돌리기는 문서 패널의 글 스냅샷(`captureDocumentText`)이 한다.
 *          - **3D 미리보기**: 미리보기 씬은 비어 있고, 두 스켈레톤의 레퍼런스 자세를 뷰포트 위에 겹쳐 선으로 긋는다(원본 파랑 · 대상 레퍼런스 회색 · 리타깃 포즈를 얹은 대상 주황).
 *            리타깃 포즈를 얹는 길은 런타임 리타기터와 같은 `RetargetPoseUtil::applyTargetPose` 하나다.
 *          - **리타깃 포즈**: 프로필의 `target_pose` 줄(대상 본 + 부모 공간 도)을 고친다. 끄는 동안 미리보기가 바로 바뀌고 놓으면 되돌리기 한 단계가 된다.
 *          - **새 프로필**: 경로 칸에 `*.retarget.xml` 리소스 id 를 적고 New 를 누르면 없는 파일은 빈 문서(`ToolAssetLoadResult::Missing`)로 열린다. 스켈레톤 · 뿌리와 골반이
 *            묶이기 전에는 저장되지 않는다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorPreviewPanel.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

namespace sw::editor
{
    class EditorProjectedLineRenderer;

    /**
     * @class RetargetPanel
     * @brief 리타깃 프로필 편집 창입니다. 파일 머리말 참고.
     */
    class RetargetPanel final : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "charactereditor.retarget";

        RetargetPanel();

        const utf8*        getPanelTitle() const override { return "Retarget Profile"; }
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 120.0f, 47.0f ); }
        void               drawDocumentContent() override;
        [[nodiscard]] bool saveDocument() override;

        const RetargetProfile&           getProfile() const { return _profile; }
        const RetargetProfileInspection& getInspection() const { return _inspection; }
        /** @brief 지난 점검이 실패한 까닭입니다. 묶였으면 비어 있습니다. */
        const string& getInspectionError() const { return _inspectionError; }

        /** @brief 자동화 감시 값이 읽는 값입니다. 패널이 그릴 때마다 게시합니다(게임 스레드). 값은 이 패널 인스턴스의 것이라 에디터마다 따로이고 패널과 함께 사라집니다. */
        struct Published
        {
            uint32 _targetPoseCount{ 0 };     ///< 프로필의 리타깃 포즈 줄 수
            uint32 _previewSegmentCount{ 0 }; ///< 지난 그리기에 미리보기가 그은 선분 수
            uint32 _chainCount{ 0 };          ///< 프로필의 사슬 수
        };
        const Published& getPublished() const { return _published; }

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

        void drawNewDocumentRow();
        void drawSkeletonRows();
        void drawPreviewSection();
        void drawRootAndPelvisRows();
        void drawOptionRows();
        void drawChainRows();
        void drawTargetPoseRows();
        void drawBonePairRows();
        void drawInspection();
        /** @brief 두 스켈레톤을 겹쳐 긋습니다(뷰포트를 그린 직후). */
        void drawSkeletonLines();
        /** @brief 편집 하나를 되돌리기에 남기고 다시 점검합니다. */
        void markEdited( const utf8* pLabel );
        /** @brief 프로필에서 편집 글(스켈레톤 경로 · 사슬 본 목록)을 다시 채우고 점검하고 미리보기 자세를 다시 만듭니다. */
        void syncFromProfile();
        /** @brief 스켈레톤 둘을 (다시) 읽고 미리보기 자세를 만듭니다. 못 읽은 쪽은 비웁니다. */
        void reloadPreviewSkeletons();
        /** @brief 읽어 둔 스켈레톤으로 세 자세(원본 · 대상 레퍼런스 · 리타깃 포즈를 얹은 대상)의 모델 공간 본 위치를 다시 만듭니다. 끄는 동안에도 부릅니다. */
        void rebuildPreviewPoses();

        RetargetProfile                      _profile;
        RetargetProfileInspection            _inspection;
        Skeleton                             _previewSource;
        Skeleton                             _previewTarget;
        vector<float3>                       _listSourcePosition;
        vector<float3>                       _listTargetReferencePosition;
        vector<float3>                       _listTargetPosePosition;
        vector<ChainText>                    _listChainText;
        string                               _inspectionError;
        string                               _status;
        fixed_string<constant::kMaxPathSize> _sourceSkeleton;
        fixed_string<constant::kMaxPathSize> _targetSkeleton;
        fixed_string<constant::kMaxPathSize> _newDocumentPath;
        fixed_string<constant::kMaxBuffer64> _arrPairName[4]; ///< 루트 원본 · 루트 대상 · 골반 원본 · 골반 대상
        fixed_string<constant::kMaxBuffer64> _newChainName;
        fixed_string<constant::kMaxBuffer64> _newPairSource;
        fixed_string<constant::kMaxBuffer64> _newPairTarget;
        fixed_string<constant::kMaxBuffer64> _newPoseBone;
        Published                            _published; ///< 감시 값이 읽는 게시 값
        uint8                                _bPreviewSourceReady : 1;
        uint8                                _bPreviewTargetReady : 1;
        uint8                                _bFramePending       : 1; ///< 스켈레톤을 새로 읽었다 — 다음 그리기에서 카메라를 담는다
    };
} // namespace sw::editor
