/**
 * @file AnimationGraphLayerEditor.h
 * @brief 애니메이션 그래프 패널의 레이어 · 본 마스크 편집 영역입니다(그래프 에셋의 "layers" · "bone_masks").
 * @details 유니티 Animator 의 Layers 탭(레이어마다 가중치 · 마스크 · 합치기)과 Avatar Mask, 언리얼 Layered Blend Per Bone 의 Branch Filter 에 해당한다.
 *          편집은 저장했을 때 다시 읽히는 값만 만든다 — 이름은 비지 않고 겹치지 않으며, 레이어가 쓰는 마스크는 지울 수 없고, 마스크는 항목이 하나 이상이다.
 *          그래서 로드 오류(`AnimationGraphAsset::parseLayers`)가 될 파일을 에디터가 쓰지 않는다. 본 이름은 스켈레톤을 모르는 이 영역이 확인하지 못한다
 *          (애니메이터가 스켈레톤에 맞출 때 오류로 알린다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"

namespace sw
{
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
         * @brief 글 입력란입니다. 편집을 마친 프레임에만 @p outCommitted 에 새 글을 넣고 true 를 돌려줍니다.
         * @details 편집하는 동안의 글은 입력란 id 별 버퍼에 둔다(편집을 시작할 때 만들고 마칠 때 지운다). 매 프레임 값에서 만든 임시 글을 넘기면
         *          편집을 마치는 프레임에 고친 글이 사라지고, 버퍼를 하나만 두면 다른 칸을 눌러 넘어가는 프레임에 앞 칸의 글을 덮는다.
         */
        bool drawCommittedText( const utf8* pID, const utf8* pCurrent, string& outCommitted );

        unordered_map<uint32, string> _mapEditBuffer; ///< 편집 중인 입력란(ImGui id)의 글
        int32                         _selectedLayer; ///< 고른 레이어 순번이다. -1 이면 없다
        int32                         _selectedMask;  ///< 고른 본 마스크 순번이다. -1 이면 없다
    };
} // namespace sw::editor
