/**
 * @file TransientLeasePool.h
 * @brief 서술 키로 고르는 대여 풀입니다 — 같은 서술의 자원을 이번 프레임에 빌리고 프레임 끝에 돌려받아, 다음 대여(다른 뷰 · 다음 프레임)가 다시 씁니다. CPU 만 쓴다.
 * @details 언리얼 `FRenderTargetPool`(서술로 찾고 프레임이 끝나면 돌려받는 풀) · RDG 트랜지언트 버퍼 풀의 자리다. GPU 자원은 부르는 쪽이 칸 번호에 매단다 —
 *          풀은 "새 칸이니 만들어라" · "오래 쉰 칸이니 놓아라" 만 답한다. 그래서 같은 풀을 텍스처 · 구조 버퍼 어느 쪽에도 쓰고 GPU 없이 시험한다.
 *          같은 크기 · 형식의 뷰 둘(분할 화면 · 같은 크기의 CCTV)은 프레임 안에서 순서대로 그리므로, 앞 뷰가 돌려준 칸을 뒤 뷰가 빌린다(`returnLeases` 를 뷰마다 부르면).
 *          프레임을 넘는 히스토리(TAA · 깊이 히스토리 · 노출)는 이 풀이 아니라 뷰가 가진다 — 다음 프레임에 같은 내용이 있어야 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class TransientLeasePool
     * @brief 서술 키 → 칸 대여 풀입니다. 렌더 스레드에서만 쓴다.
     */
    class SW_API TransientLeasePool
    {
    public:
        /** @brief 이만큼 이어서 빌리지 않은 칸은 놓을 차례다(`TransientAttachmentPool::kIdleFramesBeforeRelease` 와 같은 값 — 60 Hz 에서 약 2 초). */
        static constexpr uint32 kIdleFramesBeforeRelease = 120;

        /** @brief 대여 결과입니다. `_bCreated` 면 부르는 쪽이 이 칸의 GPU 자원을 만든다. */
        struct Lease
        {
            uint32 _slot{ 0 };
            uint8  _bCreated{ 0 };
        };

        /**
         * @brief @p descKey 서술의 칸 하나를 빌립니다. 돌려받아 쉬는 같은 서술의 칸이 있으면 그것을, 없으면 새 칸을 줍니다.
         * @details 빌린 칸은 `returnLeases` 전까지 다른 대여에 나가지 않는다 — 한 뷰의 두 첨부가 같은 서술이어도 서로 다른 칸이다(겹침 없는 나눔은 앨리어싱 계획의 몫).
         */
        Lease lease( uint64 descKey );
        /** @brief 빌린 칸을 모두 돌려받습니다. 뷰 하나를 다 기록한 뒤(다음 뷰가 다시 쓴다), 그리고 프레임 끝에 부른다. */
        void returnLeases();
        /**
         * @brief 프레임을 한 칸 나아갑니다. 이번 프레임에 빌리지 않은 칸의 쉰 프레임을 늘리고, 놓을 차례가 된 칸 번호를 @p outListReleased 에 더합니다.
         * @details 놓은 칸은 비어(서술 0) 다시 빌려 갈 때 새 칸처럼 `_bCreated` 를 준다. 칸 번호는 바뀌지 않는다(부르는 쪽 표의 번호와 같다).
         */
        void advanceFrame( vector<uint32>& outListReleased );
        /** @brief 모든 칸을 잊습니다(디바이스 해제 — GPU 자원은 부르는 쪽이 먼저 놓는다). */
        void clear() { _listSlot.clear(); }

        /** @brief 지금 GPU 자원이 있는(놓지 않은) 칸 수입니다. */
        uint32 getLiveSlotCount() const;
        /** @brief 지금 빌려 나간 칸 수입니다. */
        uint32 getLeasedSlotCount() const;

    private:
        struct Slot
        {
            uint64 _descKey{ 0 };
            uint32 _idleFrames{ 0 };
            uint8  _bLeased{ 0 };
            uint8  _bLive{ 0 }; ///< GPU 자원이 있다(놓으면 0)
            uint8  _bUsedThisFrame{ 0 };
        };
        vector<Slot> _listSlot;
    };
} // namespace sw
