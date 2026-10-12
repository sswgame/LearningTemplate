/**
 * @file DrawSnapshotHandoff.h
 * @brief UI 스레드가 낸 draw 스냅샷 슬롯을 렌더 스레드에 넘기는 약속(게시 · 렌더 시작 · 렌더 끝 · 포기)을 한 곳에 둔 상태 기계입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    /**
     * @class DrawSnapshotHandoff
     * @brief draw 스냅샷 슬롯 `constant::kMaxFrameCountInFlight` 개를 두고, UI 스레드가 쓰는 슬롯과 렌더 스레드가 읽는 슬롯이 겹치지 않게 합니다.
     * @details 값은 둘이다. 게시 슬롯(UI 가 마지막으로 다 쓴 슬롯)과 in-flight 슬롯(렌더 대기 또는 렌더 중인 슬롯, 없으면 `kInvalidSlot`).
     *          게시 슬롯은 UI 스레드만 바꾸고, in-flight 슬롯은 UI 의 게시 · 렌더 스레드의 시작과 끝 · 포기가 바꾼다.
     *
     *          한 프레임의 흐름은 이렇다.
     *          1. UI 스레드: `waitForIdle` — 지난 프레임을 렌더 스레드가 끝낼 때(`endRender`)까지 기다린다.
     *          2. UI 스레드: `waitForWriteSlot` — 게시 슬롯의 다음 슬롯을 받는다. 그 슬롯이 in-flight 이면 풀릴 때까지 기다린다.
     *          3. UI 스레드: 그 슬롯에 스냅샷을 쓴 뒤 `publish` — 게시 슬롯과 in-flight 슬롯을 그 슬롯으로 둔다("렌더 대기").
     *          4. 렌더 스레드: `beginRender` — 게시 슬롯을 읽어 in-flight 로 표시하고 그 슬롯을 그린다.
     *          5. 렌더 스레드: present 뒤 `endRender` — in-flight 표시를 푼다. 1 에서 막힌 UI 스레드가 풀린다.
     *
     *          **불변식 1. 렌더 스레드가 읽는 슬롯은 UI 스레드가 쓰지 않는다.** 렌더 스레드는 게시 슬롯만 읽고, UI 스레드는 게시 슬롯의 다음 슬롯만 쓴다.
     *          슬롯이 둘 이상이라 두 값은 다르고, 게시 슬롯은 UI 스레드만 바꾸므로 UI 가 쓰는 동안 렌더 스레드가 읽을 슬롯이 바뀌지 않는다.
     *          UI 가 다음 프레임에 쓸 슬롯이 아직 그리는 슬롯과 같아지는 경우는 2 의 기다림과 1 의 기다림이 막는다. 1 은 건너뛸 수 없다.
     *          `beginRender` 의 게시 슬롯 읽기와 in-flight 표시는 한 동작이 아니어서, 렌더 스레드가 옛 게시 슬롯을 읽은 뒤 UI 가 두 번 게시하면
     *          2 의 검사는 이미 덮인 in-flight 값만 보고 그 옛 슬롯을 내준다.
     *          UI 스레드가 게시한 프레임은 렌더 스레드가 `endRender` 로 끝낼 때까지 그 프레임의 ImGui 공유 상태(텍스처 리스트, 뷰포트)를 렌더 스레드와
     *          함께 쓰지 않는다. GL 처럼 렌더 스레드가 present 훅에서 그 상태를 건드리는 경로가 이 약속에 기댄다.
     *
     *          **불변식 2. 포기(`abandon`)는 에디터 인스턴스를 없앨 때만 한다.** 포기는 "렌더 스레드가 이 슬롯을 끝낸다" 는 약속을 렌더 스레드 없이 푼다.
     *          렌더 워커를 비운 뒤라도 인스턴스가 살아 있으면 in-flight 표시는 아직 렌더 스레드에 제출되지 않은 프레임의 것일 수 있다(게임 모듈 리로드).
     *          그때 풀면 UI 스레드가 다음 `updateUI` 에서 보조 뷰포트를 그리는 동안 렌더 스레드가 그 프레임을 그려 같은 렌더 백엔드를 두 스레드가 함께 쓴다.
     *          인스턴스를 없앨 때는 그 프레임을 그릴 쪽이 없으므로 풀어야 종료 단계의 `waitForIdle` 이 반환한다.
     *
     *          기다림은 `std::this_thread::yield` 로 돈다. 렌더 스레드가 한 프레임 안에 끝내는 짧은 기다림이기 때문이다.
     */
    class DrawSnapshotHandoff
    {
    public:
        /** @brief in-flight 슬롯이 없음을 뜻하는 값입니다. */
        static constexpr uint32 kInvalidSlot = invalid_index::kUint32;
        // 슬롯 수는 인플라이트 프레임 수와 같은 개념이므로 constant::kMaxFrameCountInFlight 를 직접 쓴다. 별칭을 두지 않는다.
        static_assert( constant::kMaxFrameCountInFlight >= 2, "draw 스냅샷은 최소 2개(더블 버퍼) 이상이어야 합니다." );

        // ------------------------------------------------------------------------------
        // 1) 수명
        // ------------------------------------------------------------------------------
        /** @brief 게시 슬롯 0, in-flight 없음으로 시작합니다. 첫 게시는 슬롯 1 에 씁니다. */
        DrawSnapshotHandoff();

        DrawSnapshotHandoff( const DrawSnapshotHandoff& )            = delete;
        DrawSnapshotHandoff& operator=( const DrawSnapshotHandoff& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) UI 스레드
        // ------------------------------------------------------------------------------
        /** @brief in-flight 슬롯이 없을 때까지 기다립니다. 렌더 스레드의 `endRender` 나 `abandon` 이 풀어 줍니다. */
        void waitForIdle() const;
        /** @brief 이번 프레임에 쓸 슬롯(게시 슬롯의 다음)을 반환합니다. 그 슬롯이 in-flight 이면 풀릴 때까지 기다립니다. */
        uint32 waitForWriteSlot() const;
        /** @brief `waitForWriteSlot` 이 준 슬롯을 다 썼음을 알립니다. 게시 슬롯과 in-flight 슬롯을 그 슬롯으로 둡니다. */
        void publish( uint32 slot );

        // ------------------------------------------------------------------------------
        // 3) 렌더 스레드
        // ------------------------------------------------------------------------------
        /** @brief 게시 슬롯을 in-flight 로 표시하고 반환합니다. 반환한 슬롯은 `endRender` 까지 UI 스레드가 쓰지 않습니다. */
        uint32 beginRender();
        /** @brief present 가 끝났음을 알립니다. in-flight 표시를 풀어 UI 스레드가 다음 프레임을 쓰게 합니다. */
        void endRender();

        // ------------------------------------------------------------------------------
        // 4) 인스턴스를 없앨 때
        // ------------------------------------------------------------------------------
        /** @brief 렌더 스레드 없이 in-flight 표시를 풉니다. 렌더 워커를 비운 뒤, 에디터 인스턴스를 없앨 때만 부릅니다(불변식 2). */
        void abandon();

        // ------------------------------------------------------------------------------
        // 5) 조회
        // ------------------------------------------------------------------------------
        /** @brief in-flight 슬롯이 없으면 true 입니다. */
        bool isIdle() const;
        /** @brief 마지막으로 게시한 슬롯입니다. */
        uint32 getPublishedSlot() const;

    private:
        atomic<uint32> _publishedSlot;
        atomic<uint32> _inFlightSlot;
    };
} // namespace sw::editor
