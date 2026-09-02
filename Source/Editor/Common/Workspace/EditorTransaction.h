#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

namespace sw::editor
{
    using EditorDocumentRestoreDelegate = Delegate<void( string_view )>;
    using EditorDocumentCaptureDelegate = Delegate<string()>;

    /**
     * @class EditorTransaction
     * @brief 스냅샷 기반의 안전하고 완전한 GameObject Undo/Redo 트랜잭션 관리자
     */
    class EditorTransaction
    {
    public:
        /** @brief 복합 트랜잭션을 시작합니다. */
        static void beginTransaction( string_view label = "" );
        /** @brief 복합 트랜잭션을 커밋하고 종료합니다. */
        static void endTransaction();
        /** @brief 복합 트랜잭션을 취소하고 버립니다. */
        static void cancelTransaction();

        /** @brief 단일 오브젝트의 상태 변경(수정) 전/후를 기록하여 Undo/Redo에 등록합니다. */
        static void recordModify( GameObjectPtr pObj, string_view beforeXml, string_view afterXml,
                                  string_view label = "Modify GameObject" );

        /** @brief 단일 오브젝트의 바이너리 상태 변경(수정) 전/후를 기록하여 Undo/Redo에 등록합니다. */
        static void recordBinaryModify( GameObjectPtr pObj, const vector<uint8>& beforeBytes, const vector<uint8>& afterBytes,
                                        string_view label = "Modify GameObject" );

        /** @brief 게임오브젝트 생성을 Undo/Redo에 등록합니다. */
        static void recordCreation( GameObjectPtr pObj, string_view label = "Create GameObject" );

        /** @brief 게임오브젝트 삭제를 Undo/Redo에 등록합니다 (삭제 전 스냅샷 보존). */
        static void recordDestruction( GameObjectPtr pObj, string_view label = "Delete GameObject" );

        /** @brief 현재 게임오브젝트의 전체 상태를 XML 스냅샷 문자열로 캡처합니다. */
        static string captureSnapshot( GameObjectPtr pObj );

        /** @brief 현재 게임오브젝트의 전체 상태를 바이너리 스냅샷 버퍼로 캡처합니다. */
        static bool captureBinarySnapshot( GameObjectPtr pObj, vector<uint8>& outBytes );

        /** @brief 문서 Undo/Redo를 스택에 올립니다. */
        static void push( Delegate<void()> undo, Delegate<void()> redo, string_view label,
                          string_view coalesceKey = {} );
        /** @brief 문서 변경 구간만 Undo/Redo에 등록합니다. capture가 있으면 현재 본문에서 복원합니다. */
        static void recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                        EditorDocumentRestoreDelegate restore, string_view coalesceKey = {} );
        static void recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                        EditorDocumentRestoreDelegate restore, EditorDocumentCaptureDelegate capture,
                                        string_view coalesceKey = {} );
    };
} // namespace sw::editor
