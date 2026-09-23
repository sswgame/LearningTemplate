#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    using EditorDocumentRestoreDelegate = Delegate<void( string_view )>;
    using EditorDocumentCaptureDelegate = Delegate<string()>;

    /**
     * @struct EditorObjectSnapshot
     * @brief 오브젝트 하나의 상태(XML)와 찍을 때의 런타임 id 입니다.
     * @details 되돌리기는 상태를 다시 읽으면서 컴포넌트를 전부 새로 만듭니다. id 를 같이 적어 두지 않으면 속성 하나만 되돌려도
     *          컴포넌트마다 새 id 가 나가, 그 컴포넌트를 가리키던 `ComponentHandle`(컴포넌트 선택 · 씬의 활성 카메라 등)이 끊깁니다.
     */
    struct EditorObjectSnapshot
    {
        string         _xml;
        ObjectIdentity _identity;
    };

    /** @brief `EditorObjectSnapshot` 의 바이너리 판입니다. */
    struct EditorObjectBinarySnapshot
    {
        vector<uint8>  _bytes;
        ObjectIdentity _identity;
    };

    /**
     * @class EditorTransaction
     * @brief 스냅샷으로 GameObject 편집을 Undo/Redo 에 기록하는 트랜잭션 관리자입니다.
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

        /** @brief 오브젝트 하나의 수정 전후 상태를 Undo/Redo 에 기록합니다. 전후 XML 이 같으면 기록하지 않습니다. */
        static void recordModify( GameObject* pObj, const EditorObjectSnapshot& before, const EditorObjectSnapshot& after,
                                  string_view label = "Modify GameObject" );

        /** @brief 오브젝트 하나의 수정 전후 바이너리 상태를 Undo/Redo 에 기록합니다. 전후 바이트가 같으면 기록하지 않습니다. */
        static void recordBinaryModify( GameObject* pObj, const EditorObjectBinarySnapshot& before, const EditorObjectBinarySnapshot& after,
                                        string_view label = "Modify GameObject" );

        /** @brief 게임오브젝트 생성을 Undo/Redo에 등록합니다. */
        static void recordCreation( GameObject* pObj, string_view label = "Create GameObject" );

        /** @brief 게임오브젝트 삭제를 Undo/Redo에 등록합니다 (삭제 전 스냅샷 보존). */
        static void recordDestruction( GameObject* pObj, string_view label = "Delete GameObject" );

        /** @brief 현재 게임오브젝트의 전체 상태를 XML 스냅샷으로 캡처합니다(런타임 id 포함). nullptr 이면 빈 스냅샷입니다. */
        static EditorObjectSnapshot captureSnapshot( const GameObject* pObj );

        /** @brief 현재 게임오브젝트의 전체 상태를 바이너리 스냅샷으로 캡처합니다(런타임 id 포함). */
        static bool captureBinarySnapshot( const GameObject* pObj, EditorObjectBinarySnapshot& outSnapshot );

        /**
         * @brief 오브젝트 수명 편집의 방향입니다. 어느 쪽이 "되살리기" 인지를 정합니다.
         * @details 생성과 삭제는 **같은 두 절차를 반대 순서로 이은 것**입니다. 예전에는 그 두 절차가 `recordCreation` 과
         *          `recordDestruction` 에 **네 벌로 복사**돼 있었습니다(없애기 두 벌 · 되살리기 두 벌, 바이트까지 같았습니다).
         *          되살리기 쪽을 한 번 고치면 반대 방향이 조용히 뒤처지고, 증상은 "Undo 는 되는데 Redo 는 안 된다" 로
         *          나타납니다. 가장 찾기 어려운 종류입니다.
         */
        enum class ObjectLifetimeEdit : uint8
        {
            Created = 0, /**< 방금 만들었습니다. Undo 가 없애고 Redo 가 되살립니다. */
            Destroyed    /**< 방금 지웠습니다. Undo 가 되살리고 Redo 가 없앱니다. */
        };

        /** @brief 생성·삭제를 한 절차로 기록합니다. 두 절차를 만들고 @p edit 이 순서를 정합니다. */
        static void recordObjectLifetime( GameObject* pObj, string_view label, ObjectLifetimeEdit edit );

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
