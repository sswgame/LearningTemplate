/**
 * @file DrawSnapshotHandoff.h
 * @brief UI 스레드가 만든 draw 스냅샷 슬롯을 렌더 스레드에 넘기는 약속을 한 곳에 둔 상태 기계입니다. 약속은 쓸 칸 받기, 게시, 렌더 시작, 렌더 끝, 포기입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw::editor
{
    /**
     * @class DrawSnapshotHandoff
     * @brief draw 스냅샷 슬롯 `constant::render::kMaxFrameCountInFlight` 개를 두고, UI 스레드가 다음 UI 프레임을 만드는 동안 렌더 스레드가 앞 스냅샷을 그리게 합니다.
     * @details 값은 셋이다. 게시 슬롯(UI 가 마지막으로 낸 슬롯), 렌더 슬롯(렌더 스레드가 지금 그리는 슬롯, 없으면 `kInvalidSlot`), 게시 대기(낸 슬롯을 렌더 스레드가 아직
     *          집지 않았다). 셋은 잠금 하나로 함께 바뀐다 — 렌더 스레드가 게시 슬롯을 읽고 렌더 슬롯으로 표시하는 사이에 UI 가 게시하면 표시가 덮이던 틈을 없앤다.
     *
     *          한 프레임의 흐름은 이렇다.
     *          1. UI 스레드: `waitForWriteSlot`. 게시 슬롯의 다음 슬롯을 받는다. 렌더 스레드가 그 슬롯을 그리는 중이면 끝날 때까지 기다린다.
     *          2. UI 스레드: 그 슬롯에 스냅샷을 쓴 뒤 `publish`. 앞에 낸 슬롯을 렌더 스레드가 아직 집지 않았으면 집을 때까지 기다리고 게시한다 — 낸 스냅샷을 건너뛰지 않는다
     *             (UI 캡처 요청 · 해제 순서가 스냅샷마다 실려 있다).
     *          3. 렌더 스레드: `beginRender`. 게시 슬롯을 렌더 슬롯으로 표시하고 게시 대기를 푼다. 새 게시가 없으면 같은 슬롯을 다시 그린다(present 마다 UI 를 그린다).
     *          4. 렌더 스레드: present 뒤 `endRender`. 렌더 슬롯을 푼다.
     *          그래서 UI 스레드는 렌더 스레드가 앞 스냅샷을 그리는 동안 다음 UI 프레임을 만들고, 한 프레임보다 앞서지 않는다(언리얼 게임 스레드가 렌더 스레드보다 한 프레임 앞서는 것과 같다).
     *
     *          **불변식 1. 렌더 스레드가 읽는 슬롯은 UI 스레드가 쓰지 않는다.** 렌더 스레드는 게시 슬롯만 집고, UI 는 게시 슬롯이 아닌 슬롯을 그것이 그려지는 중이 아닐 때만 쓴다.
     *          렌더 스레드는 살아 있는 ImGui 상태를 읽지 않고 스냅샷만 읽는다(`EditorDrawDataSnapshot` · `ImGuiRenderer::queueTextureUpdates`).
     *
     *          **불변식 2. 포기(`abandon`)는 에디터 인스턴스를 없앨 때만 한다.** 포기는 "렌더 스레드가 이 슬롯을 집고 끝낸다" 는 약속을 렌더 스레드 없이 푼다.
     *          렌더 워커를 비운 뒤라도 인스턴스가 살아 있으면 게시 대기는 아직 렌더 스레드에 제출되지 않은 프레임의 것일 수 있다(게임 모듈 리로드).
     *          인스턴스를 없앨 때는 그 프레임을 그릴 쪽이 없으므로 풀어야 종료 단계의 `waitForIdle` 이 반환한다.
     *
     *          기다림은 `ThreadUtil::yield` 로 돈다. 렌더 스레드가 한 프레임 안에 끝내는 짧은 기다림이기 때문이다.
     */
    class DrawSnapshotHandoff
    {
    public:
        /** @brief 렌더 슬롯이 없음을 뜻하는 값입니다. */
        static constexpr uint32 kInvalidSlot = constant::invalid_index::kUint32;
        // 슬롯 수는 인플라이트 프레임 수와 같은 개념이므로 constant::render::kMaxFrameCountInFlight 를 직접 쓴다. 별칭을 두지 않는다.
        static_assert( constant::render::kMaxFrameCountInFlight >= 2, "draw 스냅샷은 최소 2개(더블 버퍼) 이상이어야 합니다." );

        // ------------------------------------------------------------------------------
        // 1) 수명
        // ------------------------------------------------------------------------------
        /** @brief 게시 슬롯 0, 렌더 슬롯 없음, 게시 대기 없음으로 시작합니다. 첫 게시는 슬롯 1 에 씁니다. */
        DrawSnapshotHandoff();

        DrawSnapshotHandoff( const DrawSnapshotHandoff& )            = delete;
        DrawSnapshotHandoff& operator=( const DrawSnapshotHandoff& ) = delete;

        // ------------------------------------------------------------------------------
        // 2) UI 스레드
        // ------------------------------------------------------------------------------
        /** @brief 이번 프레임에 쓸 슬롯(게시 슬롯의 다음)을 반환합니다. 렌더 스레드가 그 슬롯을 그리는 중이면 끝날 때까지 기다립니다. */
        uint32 waitForWriteSlot() const;
        /** @brief `waitForWriteSlot` 이 준 슬롯을 다 썼음을 알립니다. 앞에 낸 슬롯을 렌더 스레드가 아직 집지 않았으면 집을 때까지 기다린 뒤 게시합니다. */
        void publish( uint32 slot );
        /**
         * @brief 낸 스냅샷을 렌더 스레드가 모두 집어 다 그릴 때까지 기다립니다. 렌더 스레드의 `endRender` 나 `abandon` 이 풀어 줍니다.
         * @details 보조 창의 스왑체인을 바꾸기 전(`ImGuiRenderer`)처럼 낸 스냅샷이 가리키는 자원을 UI 스레드가 바꿀 때만 쓴다 — 프레임마다 기다리지 않는다.
         */
        void waitForIdle() const;

        // ------------------------------------------------------------------------------
        // 3) 렌더 스레드
        // ------------------------------------------------------------------------------
        /** @brief 게시 슬롯을 렌더 슬롯으로 표시하고 반환합니다. 반환한 슬롯은 `endRender` 까지 UI 스레드가 쓰지 않습니다. */
        uint32 beginRender();
        /**
         * @brief present 가 끝났음을 알립니다. 렌더 슬롯을 풀어 UI 스레드가 그 슬롯을 쓰게 합니다.
         * @details 렌더 스레드는 프레임 본문이 멈춰 `beginRender` 를 건너뛴 프레임에도 이것을 부른다 — 그때는 낸 스냅샷을 건너뛴 것으로 치고 게시 대기를 푼다(UI 스레드가 게시에서 멈추지 않게).
         */
        void endRender();

        // ------------------------------------------------------------------------------
        // 4) 인스턴스를 없앨 때
        // ------------------------------------------------------------------------------
        /** @brief 렌더 스레드 없이 렌더 슬롯과 게시 대기를 풉니다. 렌더 워커를 비운 뒤, 에디터 인스턴스를 없앨 때만 부릅니다(불변식 2). */
        void abandon();

        // ------------------------------------------------------------------------------
        // 5) 조회
        // ------------------------------------------------------------------------------
        /** @brief 렌더 슬롯도 게시 대기도 없으면 true 입니다. */
        bool isIdle() const;
        /** @brief 마지막으로 게시한 슬롯입니다. */
        uint32 getPublishedSlot() const;

    private:
        mutable mutex           _mutex;
        uint32 _publishedSlot   SW_GUARDED_BY( _mutex );
        uint32 _renderingSlot   SW_GUARDED_BY( _mutex );
        bool _bPublishedPending SW_GUARDED_BY( _mutex );
    };
} // namespace sw::editor
