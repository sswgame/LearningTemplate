/**
 * @file CollisionResponseEditModel.h
 * @brief 충돌 응답 표 편집의 상태와 명령입니다 — 물리 설정(`physicssettings.xml`)의 프로필 × 채널 응답 격자를 보고 칸마다 막음 · 겹침 · 무시를 고칩니다. ImGui 를 모릅니다.
 * @details 문서는 파일 원문 그대로다 — 주석 · 자산 줄을 지키려고 원문을 고친다(`CatalogXMLText`), 고친 원문을 다시 읽어 `PhysicsSettings` 로 검사한다(틀리면 고치지 않는다).
 *          칸을 고치면 프로필의 `_listResponse` 에 그 채널 줄을 두거나(값이 채널 기본과 다를 때) 뺀다(같을 때) — 적힌 줄은 늘 기본과 다른 값만이다(언리얼 프리셋 편집과 같다).
 *          되돌리기는 문서 패널의 원문 단계다(`captureText` · `applyText`, 편집 알림은 `DocumentEditRecorder`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Physics/PhysicsSettings.h"

namespace sw::editor
{
    class DocumentEditRecorder;

    /** @class CollisionResponseEditModel @brief 파일 머리말 참고. */
    class CollisionResponseEditModel
    {
    public:
        CollisionResponseEditModel();

        /** @brief 편집 알림 창구를 겁니다 — 문서 패널이 만들 때 한 번 건다. */
        void bindDocumentEditRecorder( const DocumentEditRecorder& recorder ) { _pEditRecorder = &recorder; }

        /** @brief 디스크 파일 @p diskPath 를 읽습니다. 없으면 `Missing`, 읽었지만 물리 설정으로 틀리면 `Malformed` 입니다(저장은 기반이 막는다). */
        ToolAssetLoadResult loadDocument( string_view diskPath );
        /** @brief 원문 @p text 를 문서로 둡니다(테스트 · 읽기). 물리 설정으로 읽히면 true 입니다. */
        [[nodiscard]] bool loadText( string_view text );
        /** @brief 원문을 디스크 파일 @p diskPath 에 씁니다. 실패하면 상태 줄에 까닭을 두고 false 입니다. */
        [[nodiscard]] bool saveDocument( string_view diskPath );

        string captureText() const { return _text; }
        /** @brief 되돌리기 단계의 원문을 적용합니다. 읽히지 않으면 경고하고 빈 표입니다. */
        void applyText( string_view text );

        const PhysicsSettings& getSettings() const { return _settings; }
        const string&          getText() const { return _text; }
        const string&          getStatus() const { return _status; }

        /** @brief 프로필 @p profileIndex 가 채널 @p channelIndex 에 주는 응답입니다(적지 않았으면 채널 기본). */
        CollisionResponse getResponse( uint32 profileIndex, uint32 channelIndex ) const;
        /** @brief 그 칸이 채널 기본 응답을 쓰는지(프로필에 줄이 없는지)입니다. */
        bool isDefaultResponse( uint32 profileIndex, uint32 channelIndex ) const;
        /**
         * @brief 칸 하나의 응답을 @p response 로 둡니다. 바꿨으면 되돌리기 이름으로 알리고 true 입니다(같은 값 · 범위 밖 · 원문을 고칠 수 없으면 false, 까닭은 상태 줄).
         */
        bool setResponse( uint32 profileIndex, uint32 channelIndex, CollisionResponse response );
        /** @brief 칸을 다음 응답으로 돌립니다(막음 → 겹침 → 무시 → 막음). 바꿨으면 true 입니다. */
        bool cycleResponse( uint32 profileIndex, uint32 channelIndex );
        /** @brief 칸을 채널 기본 응답으로 되돌립니다(줄을 뺀다 — R10 휠클릭). 바꿨으면 true, 이미 기본이거나 범위 밖이면 false 입니다. */
        bool resetResponse( uint32 profileIndex, uint32 channelIndex );

    private:
        /** @brief 원문 @p text 가 물리 설정으로 읽히면 원문과 표를 바꾸고 true 입니다. 아니면 그대로 두고 상태 줄에 까닭을 둡니다. */
        [[nodiscard]] bool acceptText( string text );

    private:
        string                      _text;
        PhysicsSettings             _settings;
        string                      _status;
        const DocumentEditRecorder* _pEditRecorder{ nullptr };
    };
} // namespace sw::editor
