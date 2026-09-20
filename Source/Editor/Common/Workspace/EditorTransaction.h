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
        static void recordModify( const GameObjectPtr& pObj, string_view beforeXml, string_view afterXml,
                                  string_view label = "Modify GameObject" );

        /** @brief 단일 오브젝트의 바이너리 상태 변경(수정) 전/후를 기록하여 Undo/Redo에 등록합니다. */
        static void recordBinaryModify( const GameObjectPtr& pObj, const vector<uint8>& beforeBytes, const vector<uint8>& afterBytes,
                                        string_view label = "Modify GameObject" );

        /** @brief 게임오브젝트 생성을 Undo/Redo에 등록합니다. */
        static void recordCreation( const GameObjectPtr& pObj, string_view label = "Create GameObject" );

        /** @brief 게임오브젝트 삭제를 Undo/Redo에 등록합니다 (삭제 전 스냅샷 보존). */
        static void recordDestruction( const GameObjectPtr& pObj, string_view label = "Delete GameObject" );

        /** @brief 현재 게임오브젝트의 전체 상태를 XML 스냅샷 문자열로 캡처합니다. */
        static string captureSnapshot( const GameObjectPtr& pObj );

        /** @brief 현재 게임오브젝트의 전체 상태를 바이너리 스냅샷 버퍼로 캡처합니다. */
        static bool captureBinarySnapshot( const GameObjectPtr& pObj, vector<uint8>& outBytes );

        /**
         * @brief 오브젝트 수명 편집의 방향 — 어느 쪽이 "되살리기" 인지를 정합니다.
         * @details 생성과 삭제는 **같은 두 절차를 반대로 이은 것**이다. 예전에는 그 두 절차가
         *          `recordCreation` 과 `recordDestruction` 에 **네 벌로 복사**돼 있었다(없애기 두 벌 ·
         *          되살리기 두 벌, 바이트까지 같았다). 되살리기 쪽을 한 번 고치면 나머지 방향이 조용히
         *          뒤처지고, 증상은 "Undo 는 되는데 Redo 는 안 된다" 로 나타난다 — 가장 찾기 나쁜 종류다.
         */
        enum class ObjectLifetimeEdit : uint8
        {
            Created = 0, /**< 방금 만들었다 — Undo 가 없애고 Redo 가 되살린다. */
            Destroyed    /**< 방금 지웠다 — Undo 가 되살리고 Redo 가 없앤다. */
        };

        /** @brief 생성·삭제를 한 절차로 기록합니다. 두 절차를 만들고 @p edit 이 순서를 정합니다. */
        static void recordObjectLifetime( const GameObjectPtr& pObj, string_view label, ObjectLifetimeEdit edit );

        /** @brief 문서 Undo/Redo를 스택에 올립니다. */
        static void push( Delegate<void()> undo, Delegate<void()> redo, string_view label,
                          string_view coalesceKey = {} );
        /** @brief 문서 변경 구간만 Undo/Redo에 등록합니다. capture가 있으면 현재 본문에서 복원합니다. */
        static void recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                        const EditorDocumentRestoreDelegate& restore, string_view coalesceKey = {} );
        static void recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                        const EditorDocumentRestoreDelegate& restore, const EditorDocumentCaptureDelegate& capture,
                                        string_view coalesceKey = {} );
    };
} // namespace sw::editor
