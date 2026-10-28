/**
 * @file EditorDrawDataSnapshot.h
 * @brief ImGui 메인 뷰포트 DrawData를 클론해 렌더 스레드가 안전하게 그리게 합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

struct ImDrawData;

namespace sw::editor
{
    struct EditorUICaptureJob;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorDrawDataSnapshot
     * @brief EndFrame 이후 메인 DrawData를 복사합니다. 원본은 다음 NewFrame에서 무효가 됩니다.
     * @details 보조(플로팅) 뷰포트는 UI 스레드가 RenderPlatformWindowsDefault 로 직접 처리합니다.
     */
    class EditorDrawDataSnapshot
    {
    public:
        /** @brief 빈 스냅샷을 만듭니다. */
        EditorDrawDataSnapshot();
        /** @brief 클론한 DrawList를 해제합니다. */
        ~EditorDrawDataSnapshot();

        EditorDrawDataSnapshot( const EditorDrawDataSnapshot& )            = delete;
        EditorDrawDataSnapshot& operator=( const EditorDrawDataSnapshot& ) = delete;

        /**
         * @brief 현재 ImGui 컨텍스트의 메인 뷰포트 DrawData를 복사하고, 이 스냅샷의 번호(1 부터 늘기만 한다)를 적습니다.
         * @param pCaptureJob 에디터 UI 캡처 요청입니다. 있으면 모든 뷰포트(플로팅 창 포함)를 합친 DrawData 도 복사합니다(`findCaptureDrawData`).
         */
        void capture( uint64 sequence, const EditorUICaptureJob* pCaptureJob );
        /** @brief 소유한 클론을 모두 지웁니다. */
        void clear();
        /** @brief 캡처된 메인 DrawData가 있으면 true입니다. */
        bool isValid() const;
        /** @brief 렌더 스레드가 그릴 메인 DrawData입니다. 없으면 nullptr입니다. */
        ImDrawData* getMainDrawData();
        /** @brief 캡처 요청이 있던 프레임이면 모든 뷰포트를 합친 DrawData 입니다(화면 좌표, 감싸는 사각형이 표시 영역). 없으면 nullptr 입니다. */
        ImDrawData* findCaptureDrawData();
        /** @brief `findCaptureDrawData` 가 있을 때 그 요청입니다. */
        const EditorUICaptureJob& getCaptureJob() const;
        /** @brief `capture` 에 준 번호입니다. 비어 있으면 0 입니다(`EditorDrawReleaseQueue` 가 이 번호로 해제를 넘깁니다). */
        uint64 getSequence() const { return _sequence; }

    private:
        struct Impl;
        unique_ptr<Impl> _pImpl;
        uint64           _sequence;
    };
} // namespace sw::editor
