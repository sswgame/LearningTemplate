/**
 * @file AnimationGraphLayerEditor.h
 * @brief 애니메이션 그래프 패널의 레이어 · 본 마스크 편집 영역입니다(그래프 에셋의 "layers" · "bone_masks").
 * @details 유니티 Animator 의 Layers 탭(레이어마다 가중치 · 마스크 · 합치기)과 Avatar Mask, 언리얼 Layered Blend Per Bone 의 Branch Filter 에 해당한다.
 *          편집은 저장했을 때 다시 읽히는 값만 만든다 — 이름은 비지 않고 겹치지 않으며, 레이어가 쓰는 마스크는 지울 수 없고, 마스크는 항목이 하나 이상이다.
 *          그래서 로드 오류(`AnimationGraphAsset::parseLayers`)가 될 파일을 에디터가 쓰지 않는다. 그래프는 스켈레톤을 모르므로 본 이름은 고른 검증 스켈레톤
 *          (`_skeletonPath`, 그래프에 저장하지 않는다)이 있을 때 계층 보기(`BoneMaskHierarchy`)가 맞춰 보고, 없는 본은 이름으로 알린다(애니메이터도 스켈레톤에 맞출 때 오류로 알린다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Panels/Animation/Model/BoneMaskHierarchy.h"

namespace sw
{
    class Skeleton;

    struct AnimationBoneMaskDef;
    struct AnimationGraphLayerDef;

    class AnimationGraphAsset;
} // namespace sw

namespace sw::editor
{
    /** @brief 레이어 · 본 마스크 편집 영역입니다. 패널이 하나 들고 그래프를 빌려 그립니다. */
    class AnimationGraphLayerEditor
    {
    public:
        AnimationGraphLayerEditor();

        /**
         * @brief 레이어와 본 마스크 목록, 고른 항목의 값을 그립니다.
         * @return 고쳤으면 Undo 에 남길 이름이고, 고치지 않았으면 nullptr 입니다.
         */
        const utf8* draw( AnimationGraphAsset& inoutGraph );
        /** @brief 고른 항목을 풉니다. 문서를 다시 읽거나 바꿀 때 부릅니다. */
        void clearSelection();
        /** @brief 계층 보기가 지난 그리기에 펼친 본 줄 수입니다(감시 값 `Editor.AnimationGraphMaskBoneRowCount`). 검증 스켈레톤이 없으면 0 입니다. */
        uint32 getBoneRowCount() const { return static_cast<uint32>( _listBoneRow.size() ); }

    private:
        const utf8* drawLayers( AnimationGraphAsset& inoutGraph );
        const utf8* drawBoneMasks( AnimationGraphAsset& inoutGraph );
        /** @brief 레이어 목록과 추가 · 삭제 · 순서 버튼입니다. 편집했으면 되돌리기 이름을 돌려줍니다. */
        const utf8* drawLayerList( AnimationGraphAsset& inoutGraph );
        /** @brief 고른 레이어의 이름 · 클립 · 마스크 행입니다. 편집했으면 @p pInoutEdit 에 되돌리기 이름을 둡니다. */
        void drawLayerIdentityRows( AnimationGraphAsset& inoutGraph, AnimationGraphLayerDef& inoutLayer, const utf8*& pInoutEdit );
        /** @brief 고른 레이어의 가중치 · 블렌드 · 반복 · 반감기 행입니다. */
        void drawLayerBlendRows( AnimationGraphLayerDef& inoutLayer, const utf8*& pInoutEdit );
        /** @brief 본 마스크 목록과 추가 · 삭제 버튼입니다. */
        const utf8* drawMaskList( AnimationGraphAsset& inoutGraph );
        /** @brief 고른 마스크의 이름과 프리셋 파일 행입니다. 이름을 바꾸면 그 마스크를 쓰는 레이어도 따라 옮깁니다. */
        void drawMaskSourceRows( AnimationGraphAsset& inoutGraph, AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit );
        /** @brief 고른 마스크의 본 항목 표와 항목 추가 · 삭제입니다. 프리셋 마스크는 보이기만 합니다. */
        void drawMaskEntries( AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit );
        /**
         * @brief 검증 스켈레톤 칸과, 그 스켈레톤 계층 위의 본마다 최종 가중치(막대) · 출처, 고른 본의 칠하기 · 초기화입니다(언리얼 Blend Profile · 유니티 Avatar Mask 의 트리).
         * @details 가중치 막대는 데이터라 강조색이고, 이 본의 항목은 본문 색 · 물려받은 값은 흐린 색이다. 프리셋 마스크는 보이기만 한다(그 파일이 원본이다).
         */
        void drawMaskHierarchy( AnimationBoneMaskDef& inoutMask, const utf8*& pInoutEdit );
        /** @brief 칸의 검증 스켈레톤 경로를 읽습니다(칸을 떠날 때 한 번 — 치는 글자마다 읽으면 반쪽 경로마다 읽기 오류가 남는다). 읽지 못하면 스켈레톤을 비웁니다. */
        void applySkeletonPath();
        /** @brief 본 줄 하나(들여쓰기 · 가중치 막대 · 이름)를 그립니다. 누르면 그 본을 고릅니다. */
        void drawBoneRow( const BoneMaskBoneRow& row, uint32 rowIndex );
        /**
         * @brief 글 입력란입니다. 편집을 마친 프레임에만 @p outCommitted 에 새 글을 넣고 true 를 돌려줍니다.
         * @details 편집하는 동안의 글은 입력란 id 별 버퍼에 둔다(편집을 시작할 때 만들고 마칠 때 지운다). 매 프레임 값에서 만든 임시 글을 넘기면
         *          편집을 마치는 프레임에 고친 글이 사라지고, 버퍼를 하나만 두면 다른 칸을 눌러 넘어가는 프레임에 앞 칸의 글을 덮는다.
         */
        bool drawCommittedText( const utf8* pID, const utf8* pCurrent, string& outCommitted );

    private:
        unordered_map<uint32, string>           _mapEditBuffer;   ///< 편집 중인 입력란(ImGui id)의 글
        int32                                   _selectedLayer;   ///< 고른 레이어 순번이다. -1 이면 없다
        int32                                   _selectedMask;    ///< 고른 본 마스크 순번이다. -1 이면 없다
        fixed_string<constant::buffer::kMax256> _skeletonPath;    ///< 마스크를 맞춰 볼 검증 스켈레톤(`*.skeleton.xml`)이다. 그래프에 저장하지 않는다
        shared_ptr<const Skeleton>              _pSkeleton;       ///< 검증 스켈레톤(공유 캐시의 한 벌)이다. 읽지 못했으면 nullptr 이다
        vector<BoneMaskBoneRow>                 _listBoneRow;     ///< 고른 마스크를 스켈레톤에 펼친 줄(그리기마다 다시 만든다)
        vector<hashed_string>                   _listMissingBone; ///< 마스크가 가리키지만 스켈레톤에 없는 본
        hashed_string                           _selectedBone;    ///< 계층 보기에서 고른 본이다. 비면 없다
        float32                                 _paintWeight;     ///< 칠할 가중치(0 ~ 1)
    };
} // namespace sw::editor
