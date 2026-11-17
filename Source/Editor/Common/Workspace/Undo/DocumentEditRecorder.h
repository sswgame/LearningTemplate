/**
 * @file Editor/Common/Workspace/Undo/DocumentEditRecorder.h
 * @brief 문서 패널의 Model 이 편집을 알리는 창구입니다 — 문서 패널(`EditorDocumentPanel`)이 만들어 Model 에 넘기고, 알림은 그 패널의 문서 되돌리기 · dirty 로 간다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @class DocumentEditRecorder
     * @brief 문서 편집 알림(되돌리기 기록 + dirty)의 창구입니다. Model 이 문서를 바꾼 뒤 `notifyEdited` 로 알린다.
     * @details 예전에는 패널 View 가 `notifyDocumentEdited` 를 직접 불러 편집 판정이 View 에 흩어졌다(감사 E-16). 이제 그 함수는 문서 패널 안에 숨고, 알림은
     *          이 창구를 받은 Model 만 한다 — View 는 이 헤더를 include 할 수 없어(`EditorLayers.toml` `[panelViewBan]` 의 작업 공간 헤더) 앞 선언만 보고 넘기기만 한다.
     *          언리얼에서 편집 함수가 `Modify()` 를 부르는 쪽이 위젯이 아니라 데이터 쪽인 것, Godot 이 `EditorUndoRedoManager` 를 플러그인(모델)에 넘기는 것과 같은 자리다.
     */
    class SW_EDITOR_API DocumentEditRecorder final
    {
    public:
        using RecordDelegate = Delegate<void( string_view label )>;

        explicit DocumentEditRecorder( RecordDelegate record )
            : _record{ std::move( record ) }
        {
        }
        DocumentEditRecorder( const DocumentEditRecorder& )            = delete;
        DocumentEditRecorder& operator=( const DocumentEditRecorder& ) = delete;

        /** @brief 문서가 바뀌었음을 알립니다(@p label 은 되돌리기 이름). 위젯이 활성인 동안은 상호작용 구간으로 쥐어 끝에 한 단계로 기록한다. */
        void notifyEdited( string_view label ) const { _record( label ); }

    private:
        RecordDelegate _record;
    };
} // namespace sw::editor
