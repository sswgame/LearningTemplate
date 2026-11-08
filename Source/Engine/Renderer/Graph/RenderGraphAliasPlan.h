/**
 * @file RenderGraphAliasPlan.h
 * @brief 수명이 겹치지 않는 트랜지언트 첨부가 텍스처 하나를 나눠 쓰는 계획(메모리 앨리어싱)을 세웁니다. CPU 만 쓴다.
 * @details 언리얼 RDG 의 트랜지언트 할당기가 패스 수명이 겹치지 않는 텍스처를 같은 메모리에 놓는 자리다. 여기서는 **같은 서술(형식 · 크기 · 용도 · 클리어 값)** 끼리만 나눈다 —
 *          서술이 같아 텍스처 핸들 하나를 그대로 넘기면 되고 네 백엔드(DX11 · GL 포함)가 같은 길이다. 서술이 다른 첨부를 한 힙에 겹쳐 놓는 진짜 앨리어싱
 *          (DX12 placed resource · Vulkan 메모리 공유 + 앨리어싱 배리어)은 이 계획의 주인 · 별칭 묶음을 크기 등급으로 받아 쓰는 다음 층이다.
 *          수명은 실행 순서가 아니라 **레벨**로 잰다 — 같은 레벨의 패스는 병렬로 기록되고 GPU 에서 겹칠 수 있다. 앞 수명의 마지막 레벨 < 뒤 수명의 첫 레벨이면 나눈다
 *          (뒤의 첫 사용 앞에서 레벨 프롤로그가 상태 전이를 내므로 그 전이가 앞의 읽기를 기다린다).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    /**
     * @struct RenderGraphAliasCandidate
     * @brief 나눠 쓸 수 있는 첨부 하나입니다. 서술 키가 같고 레벨 구간이 겹치지 않는 후보끼리 텍스처를 나눈다.
     */
    struct RenderGraphAliasCandidate
    {
        hashed_string _name;
        uint64        _descKey{ 0 };    ///< 형식 · 나눗수 · RW 용도 · 클리어 값의 해시 — 같아야 같은 텍스처를 쓸 수 있다
        uint32        _firstLevel{ 0 }; ///< 처음 쓰는(쓰기 또는 읽기) 패스의 레벨
        uint32        _lastLevel{ 0 };  ///< 마지막으로 쓰는 패스의 레벨
        uint8         _bPlaceable{ 0 }; ///< 서술이 다른 첨부와 힙 바이트를 나눌 수 있다 — 프레임의 첫 쓰기가 지우기(`_loadOp` Clear)이고 RW 가 아니다
    };

    /**
     * @struct RenderGraphAliasPlan
     * @brief 계획의 결과입니다. 별칭 이름 → 텍스처를 가진 첨부 이름. 맵에 없는 후보는 자기 텍스처를 가진다.
     */
    struct SW_API RenderGraphAliasPlan
    {
        /**
         * @brief 후보 목록에서 계획을 세웁니다(서술 키마다 레벨 구간 색칠 — 앞에서부터 끝난 칸을 다시 쓴다).
         * @details 같은 서술 키 안에서 첫 레벨 순으로(같으면 목록 순으로) 보며, 마지막 레벨이 이 후보의 첫 레벨보다 작은 칸을 다시 쓴다.
         *          결과는 입력 순서에 따라 결정적이다(같은 파이프라인이면 같은 계획 — 뷰마다 · 프레임마다 같다).
         */
        static RenderGraphAliasPlan build( const vector<RenderGraphAliasCandidate>& listCandidate );

        unordered_map<hashed_string, hashed_string> _mapAliasToOwner;
        uint32                                      _ownerCount{ 0 }; ///< 실제로 만들 텍스처 수(후보 수 - 별칭 수)
        /**
         * @brief 힙에 겹쳐 놓을 수 있는 텍스처(주인)와 그 텍스처를 쓰는 모든 이름을 합친 레벨 구간입니다 — 칸의 모든 이름이 `_bPlaceable` 일 때만 든다.
         * @details 능력 `_bPlacedTextures` 가 있는 백엔드에서 뷰 풀이 크기 · 정렬로 힙 오프셋을 정한다(`RenderGraphMemoryPlan`, `FrameViewResources::updateViewTransients`).
         */
        vector<RenderGraphAliasCandidate> _listPlaceable;

        /** @brief @p name 이 텍스처를 빌리는 별칭이면 주인 이름, 아니면 빈 이름입니다. */
        hashed_string findOwner( const hashed_string& name ) const
        {
            const auto it = _mapAliasToOwner.find( name );
            return it != _mapAliasToOwner.end() ? it->second : hashed_string{};
        }
    };

} // namespace sw
