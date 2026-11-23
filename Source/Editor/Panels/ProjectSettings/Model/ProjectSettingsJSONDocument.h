/**
 * @file ProjectSettingsJSONDocument.h
 * @brief 반사 형식이 없는 설정 JSON(린트 · 격리 · 환경 표, C++ 가 직접 읽는 개발 표) 하나의 편집 상태입니다. 값을 형식별 칸으로 고치고 바뀐 글 범위만 고쳐 씁니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Undo/EditorDocumentState.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    /**
     * @enum ProjectSettingsReadOnlyReason
     * @brief 표 페이지가 파일을 고치지 않는 까닭입니다. `None` 이면 고칠 수 있다.
     */
    enum class ProjectSettingsReadOnlyReason : uint8
    {
        None,               ///< 고칠 수 있다
        Generated,          ///< 도구가 만든 파일(`*.generated.json`) — 고쳐도 다음 생성이 덮는다
        LayoutNotPreserved, ///< 원문에서 노드 범위를 찾지 못했고(겹친 키 등) 다시 쓴 글도 원문과 다르다 — 저장하면 파일 모양이 바뀌므로 고치지 않는다
    };

    /**
     * @struct ProjectSettingsJSONSpan
     * @brief 노드가 읽은 원문에서 차지한 글 범위(바이트 위치)입니다. 연 뒤에 더한 노드는 범위가 없다.
     */
    struct ProjectSettingsJSONSpan
    {
        static constexpr uint32 kNone = ~0u;

        uint32 _memberBegin{ kNone }; ///< 객체 멤버면 키의 여는 따옴표, 배열 원소면 값의 시작
        uint32 _keyEnd{ kNone };      ///< 객체 멤버면 키의 닫는 따옴표 다음, 아니면 kNone
        uint32 _valueBegin{ kNone };
        uint32 _valueEnd{ kNone };

        [[nodiscard]] bool isPresent() const { return _valueBegin != kNone; }
    };

    /**
     * @struct ProjectSettingsJSONNode
     * @brief 편집 트리의 값 하나입니다. 자식은 문서의 노드 배열 번호로 든다(뺀 노드는 배열에 남지만 어느 부모도 가리키지 않아 쓰이지 않는다).
     */
    struct ProjectSettingsJSONNode
    {
        JSONType       _type{ JSONType::Null };
        string         _key;       ///< 객체 멤버면 키, 배열 원소 · 루트면 비었다
        string         _text;      ///< 글 · 수 · 불의 글(따옴표 안쪽, `0.01`, `true`). 컨테이너 · null 은 비었다
        vector<uint32> _listChild; ///< 배열 · 객체의 자식 노드 번호(순서 그대로)

        ProjectSettingsJSONSpan _source;          ///< 원문 범위 — 바뀌지 않은 노드는 이 글을 그대로 쓴다
        string                  _textAtOpen;      ///< 열 때의 `_text`(말단이 바뀌었는지 본다)
        vector<uint32>          _listChildAtOpen; ///< 열 때의 `_listChild`(원소를 더하거나 뺐는지 본다)

        /** @brief 편집이 새로 더한 노드입니다 — 원문 범위 · 열 때의 값이 없다. 부분 집합 초기화(빠진 필드 경고)를 쓰지 않게 한 곳에서 만든다. */
        [[nodiscard]] static ProjectSettingsJSONNode makeAdded( JSONType type, string key )
        {
            ProjectSettingsJSONNode node{};
            node._type = type;
            node._key  = std::move( key );
            return node;
        }
    };

    /**
     * @class ProjectSettingsJSONDocument
     * @brief 스키마 없는 설정 파일의 편집 문서입니다 — Godot ProjectSettings 가 형식 없는 값을 형식별 칸으로 고치는 자리(반사 형식이 있으면 `ProjectSettingsDocument` 가 프로퍼티 그리드로 고친다).
     * @details 읽은 글과 지금 트리를 쓴 글이 다르면 바뀐 것이다. 쓰기는 VS Code 의 settings.json 편집(jsonc-parser `modify`)처럼 바뀐 범위만 고친다 —
     *          바뀌지 않은 노드는 원문 글, 바뀐 말단은 그 값 글만, 원소를 더하거나 뺀 컨테이너는 원문의 구분자(쉼표 · 줄바꿈 · 들여쓰기)를 다시 쓴다.
     *          손으로 맞춘 한 줄 객체도 고친 값 밖은 바이트까지 그대로다(`ProjectSettingsJSONDocumentTest.EveryConfigFileRoundTrips` · `HandLayoutIsKeptOnEdit`).
     *          새로 더한 컨테이너만 파일의 들여쓰기(첫 들여쓴 줄의 칸 수, 없으면 한 줄)로 쓴다. 도구가 만든 파일(`*.generated.json`)은 읽기 전용이다.
     */
    class ProjectSettingsJSONDocument
    {
    public:
        /** @brief 루트 노드 번호입니다(열려 있을 때만 뜻이 있다). */
        static constexpr uint32 kRootNode = 0;

        ProjectSettingsJSONDocument();

        /** @brief @p absolutePath 를 읽어 트리를 만듭니다. 읽지 못하거나 JSON 이 틀리면 그 이유이고 문서는 닫힌다. */
        [[nodiscard]] expected<void> open( string_view absolutePath );
        /** @brief 글 @p text 를 파일 @p absolutePath 의 내용으로 열어 트리를 만듭니다(`open` 의 본체 — 테스트가 디스크 없이 부른다). */
        [[nodiscard]] expected<void> openText( string_view absolutePath, string_view text );
        /** @brief 연 파일의 편집본 글 @p text 를 저장하지 않은 편집으로 엽니다(자동 저장 사본 복원 — 저장 기준은 디스크 글 그대로다). 열려 있지 않거나 글이 틀리면 false 이고 문서는 그대로입니다. */
        [[nodiscard]] bool restoreText( string_view text );
        /** @brief 파일을 다시 읽어 편집을 버립니다. */
        [[nodiscard]] expected<void> revert();
        /** @brief 지금 트리를 파일에 씁니다. 읽기 전용이거나 닫혀 있거나 쓰지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool saveDocument();

        /** @brief 열려 있으면 true 입니다. */
        [[nodiscard]] bool isOpen() const { return _listNode.empty() == false; }
        /** @brief 고치지 않는 파일이면 true 입니다(까닭은 `getReadOnlyReason`). */
        [[nodiscard]] bool isReadOnly() const { return _readOnlyReason != ProjectSettingsReadOnlyReason::None; }
        /** @brief 고치지 않는 까닭입니다. */
        [[nodiscard]] ProjectSettingsReadOnlyReason getReadOnlyReason() const { return _readOnlyReason; }
        /** @brief 지금 트리를 쓴 글이 마지막으로 읽거나 쓴 글과 다르면 true 입니다. */
        [[nodiscard]] bool isModified() const;
        /** @brief 파일에 쓸 글입니다(들여쓰기 · 끝 줄바꿈은 읽은 파일을 따른다). 열려 있지 않으면 빈 글입니다. */
        [[nodiscard]] string makeText() const;
        /** @brief 루트에서 닿는 말단 값(글 · 수 · 불 · null) 수입니다(감시 값). */
        [[nodiscard]] uint32 countLeaves() const;
        /** @brief 노드 @p node 입니다. */
        [[nodiscard]] const ProjectSettingsJSONNode& getNode( uint32 node ) const { return _listNode[node]; }

        /**
         * @brief 말단 @p node 의 값을 @p text 로 바꿉니다. 수는 JSON 수 글, 불은 `true` · `false` 여야 한다. 맞지 않거나 컨테이너 · null · 읽기 전용이면 바꾸지 않고 false 입니다.
         * @details 글 칸을 치는 동안의 연속 편집은 되돌리기 한 단계로 합친다 — 칸을 떠나면 `endTextEdit` 를 부른다.
         */
        [[nodiscard]] bool setLeafText( uint32 node, string_view text );
        /** @brief 글 칸 하나의 편집이 끝났습니다. 다음 편집은 같은 칸이어도 새 되돌리기 단계입니다. */
        void endTextEdit() { _state.endCoalescing(); }

        /** @brief 편집 한 단계를 되돌립니다(창 안 되돌리기 — 패널이 자기 명령 스택에서 부른다). 기록이 없으면 false 입니다. */
        [[nodiscard]] bool undo() { return _state.undo( _listNode ); }
        /** @brief 되돌린 단계를 다시 합니다. 기록이 없으면 false 입니다. */
        [[nodiscard]] bool redo() { return _state.redo( _listNode ); }
        /** @brief 지금까지 기록한 되돌리기 단계 수입니다(합친 편집은 세지 않고 되돌려도 줄지 않는다) — 패널이 단계마다 명령 하나를 올린다. */
        [[nodiscard]] uint32 getRecordedUndoStepCount() const { return _recordedUndoStepCount; }
        /**
         * @brief 배열 @p arrayNode 끝에 원소를 더하고 그 번호를 돌려줍니다. 원소 모양은 마지막 원소와 같고(말단은 빈 값 — 글 "", 수 0, 불 false; 객체는 같은 키에 빈 값; 배열은 빈 배열),
         *        빈 배열이면 빈 글입니다. 배열이 아니거나 읽기 전용이면 없음입니다.
         */
        [[nodiscard]] optional<uint32> addArrayItem( uint32 arrayNode );
        /** @brief 객체 @p objectNode 끝에 키 @p key 의 빈 글 멤버를 더합니다. 키가 비었거나 이미 있거나 객체가 아니거나 읽기 전용이면 false 입니다. */
        [[nodiscard]] bool addObjectMember( uint32 objectNode, string_view key );
        /** @brief @p parentNode 의 @p childIndex 번째 자식을 뺍니다. 범위 밖이거나 읽기 전용이면 false 입니다. */
        [[nodiscard]] bool removeChild( uint32 parentNode, size_t childIndex );

    private:
        uint32 appendNode( const JSONValue& value, string_view key );
        uint32 appendEmptyLike( uint32 sampleNode, string_view key );
        /** @brief 원문 @p inoutPos 부터 노드 @p node 의 범위를 찾아 적고 그 끝으로 옮깁니다. 트리와 원문이 어긋나면(겹친 키 등) false 입니다. */
        [[nodiscard]] bool locateSourceSpans( uint32 node, size_t& inoutPos );
        /** @brief 노드 @p node 의 글을 덧붙입니다. 범위가 없는 노드는 파일 들여쓰기로 새로 쓰고 줄마다 @p lineIndent 를 앞에 붙인다. */
        [[nodiscard]] bool appendValueText( uint32 node, string_view lineIndent, string& inoutText ) const;
        /** @brief 원소를 더하거나 뺀 컨테이너 @p node 를 원문의 구분자로 다시 씁니다. */
        [[nodiscard]] bool appendChangedContainerText( uint32 node, string& inoutText ) const;
        /** @brief 범위가 없는 노드 @p node 를 파일 들여쓰기로 씁니다. */
        [[nodiscard]] bool appendSerializedText( uint32 node, string_view lineIndent, string& inoutText ) const;
        /** @brief 편집 직전 트리를 되돌리기 기록에 올립니다. */
        void recordEdit( string_view coalesceKey = {} );

    private:
        vector<ProjectSettingsJSONNode> _listNode;
        string                          _absolutePath;
        string                          _sourceText; ///< 열 때 읽은 글 — 노드 범위(`_source`)가 가리킨다
        /// 마지막으로 읽거나 저장한 글과 트리 스냅숏 되돌리기 기록. 노드가 원문 범위를 들어 되돌린 트리도 바뀐 범위만 고쳐 쓴다
        EditorDocumentState<vector<ProjectSettingsJSONNode>> _state;
        uint32                                               _recordedUndoStepCount;
        int32                                                _indent;
        ProjectSettingsReadOnlyReason                        _readOnlyReason;
        uint8                                                _bTrailingNewLine : 1;
    };
} // namespace sw::editor
