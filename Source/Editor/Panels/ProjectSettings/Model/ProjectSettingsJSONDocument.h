/**
 * @file ProjectSettingsJSONDocument.h
 * @brief 반사 형식이 없는 설정 JSON(린트 · 격리 · 환경 표, C++ 가 직접 읽는 개발 표) 하나의 편집 상태입니다. 값을 형식별 칸으로 고치고 파일의 모양(들여쓰기 · 끝 줄바꿈 · 키 순서 · 수 글)을 지켜 씁니다(ImGui 없음).
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
     * @struct ProjectSettingsJSONNode
     * @brief 편집 트리의 값 하나입니다. 자식은 문서의 노드 배열 번호로 든다(뺀 노드는 배열에 남지만 어느 부모도 가리키지 않아 쓰이지 않는다).
     */
    struct ProjectSettingsJSONNode
    {
        JSONType       _type{ JSONType::Null };
        string         _key;       ///< 객체 멤버면 키, 배열 원소 · 루트면 비었다
        string         _text;      ///< 글 · 수 · 불의 글(따옴표 안쪽, `0.01`, `true`). 컨테이너 · null 은 비었다
        vector<uint32> _listChild; ///< 배열 · 객체의 자식 노드 번호(순서 그대로)
    };

    /**
     * @class ProjectSettingsJSONDocument
     * @brief 스키마 없는 설정 파일의 편집 문서입니다 — Godot ProjectSettings 가 형식 없는 값을 형식별 칸으로 고치는 자리(반사 형식이 있으면 `ProjectSettingsDocument` 가 프로퍼티 그리드로 고친다).
     * @details 읽은 글과 지금 트리를 쓴 글이 다르면 바뀐 것이다. 쓰기는 파일의 들여쓰기(첫 들여쓴 줄의 칸 수, 없으면 한 줄)와 끝 줄바꿈을 지키고 수는 파일의 글 그대로 둔다 —
     *          고치지 않은 파일은 쓴 글이 읽은 글과 바이트까지 같다(`ProjectSettingsJSONDocumentTest.EveryConfigFileRoundTrips`). 도구가 만든 파일(`*.generated.json`)은 읽기 전용이다.
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
        /** @brief 파일을 다시 읽어 편집을 버립니다. */
        [[nodiscard]] expected<void> revert();
        /** @brief 지금 트리를 파일에 씁니다. 읽기 전용이거나 닫혀 있거나 쓰지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool saveDocument();

        /** @brief 열려 있으면 true 입니다. */
        [[nodiscard]] bool isOpen() const { return _listNode.empty() == false; }
        /** @brief 도구가 만든 파일(`*.generated.json`)이라 고치지 않으면 true 입니다 — 고쳐도 다음 생성이 덮는다. */
        [[nodiscard]] bool isReadOnly() const { return _bReadOnly != 0; }
        /** @brief 지금 트리를 쓴 글이 마지막으로 읽거나 쓴 글과 다르면 true 입니다. */
        [[nodiscard]] bool isModified() const;
        /** @brief 파일에 쓸 글입니다(들여쓰기 · 끝 줄바꿈은 읽은 파일을 따른다). 열려 있지 않으면 빈 글입니다. */
        [[nodiscard]] string makeText() const;
        /** @brief 루트에서 닿는 말단 값(글 · 수 · 불 · null) 수입니다(감시 값). */
        [[nodiscard]] uint32 countLeaves() const;
        /** @brief 노드 @p node 입니다. */
        [[nodiscard]] const ProjectSettingsJSONNode& getNode( uint32 node ) const { return _listNode[node]; }

        /** @brief 말단 @p node 의 값을 @p text 로 바꿉니다. 수는 JSON 수 글, 불은 `true` · `false` 여야 한다. 맞지 않거나 컨테이너 · null · 읽기 전용이면 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool setLeafText( uint32 node, string_view text );
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

    private:
        vector<ProjectSettingsJSONNode> _listNode;
        string                          _absolutePath;
        EditorDocumentState<string>     _state; ///< 마지막으로 읽거나 저장한 글
        int32                           _indent;
        uint8                           _bTrailingNewLine : 1;
        uint8                           _bReadOnly        : 1;
    };
} // namespace sw::editor
