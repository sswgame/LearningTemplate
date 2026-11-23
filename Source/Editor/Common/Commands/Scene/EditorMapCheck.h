/**
 * @file EditorMapCheck.h
 * @brief 맵 검사의 등록점입니다(`SW_EDITOR_MAP_CHECK`) — 오브젝트 하나로 판정할 수 없는 검사(카탈로그 파일 · 키트 데이터)를 Check Map 단추가 함께 돌립니다. ImGui 를 모릅니다.
 * @details 오브젝트마다의 검증은 컴포넌트의 검증 함수(`ObjectValidation`)가 한다 — 여기는 그 밖의 것만 둔다(언리얼 `UEditorValidatorBase` 등록 자리). 결과는 같은 표(`SceneValidationIssues`)에
 *          검사 id 를 출처로 남아, 다시 돌리면 그 검사의 결과만 바뀐다. 키트는 자기 Editor 확장에서 등록한다 — 확장을 내리면 검사도 빠진다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorExtensionRegistry.h"

#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorMapCheckReport
     * @brief 검사 하나가 결과를 적는 곳이자 검사할 것의 목록입니다.
     */
    class SW_EDITOR_API EditorMapCheckReport
    {
    public:
        /** @param listPackAsset 활성 팩의 데이터 XML 리소스 id 들입니다(모든 검사가 같은 목록을 본다 — 검사마다 폴더를 훑지 않는다). */
        explicit EditorMapCheckReport( const vector<string>& listPackAsset );

        /** @brief 결과 한 줄을 적습니다. @p label 은 무엇의 문제인지(리소스 id 면 줄을 두 번 눌러 콘텐츠 브라우저에서 찾는다). */
        void                           add( ValidationSeverity severity, string_view label, string_view message );
        const vector<string>&          getPackAssets() const { return *_pListPackAsset; }
        const vector<ValidationIssue>& getIssues() const { return _listIssue; }

    private:
        const vector<string>*   _pListPackAsset;
        vector<ValidationIssue> _listIssue;
    };

    /** @brief 맵 검사 하나의 등록 항목입니다. 검사의 .cpp 가 `SW_EDITOR_MAP_CHECK` 로 둡니다. */
    struct EditorMapCheckRegistration : EditorExtensionRegistration
    {
        SW_EDITOR_EXTENSION_KIND( "mapCheck" );

        /** @brief 씬 @p manager 와 활성 팩을 보고 결과를 적습니다(메인 스레드, Check Map 단추). */
        void ( *_pfnCheck )( const GameObjectManager& manager, EditorMapCheckReport& outReport );
    };

    /** @brief 등록된 맵 검사를 돌립니다. */
    struct SW_EDITOR_API EditorMapCheck
    {
        /** @brief 검사를 모두 돌려 @p manager 의 결과 표에서 검사마다의 결과를 바꿉니다. 적힌 결과 수를 돌려줍니다. */
        static uint32 runAll( GameObjectManager& manager );
        /** @brief 검사 @p registrationID 의 결과 출처 번호입니다(오브젝트 id 와 겹치지 않게 맨 위 비트를 켠다). */
        static uint64 makeSourceID( string_view registrationID );
        /** @brief 출처 @p sourceID 가 오브젝트가 아니라 등록된 검사면 true 입니다. */
        static bool isCheckSource( uint64 sourceID );
    };
} // namespace sw::editor

/** @brief 맵 검사 하나를 등록합니다. @p pID 는 종류 안에서 유일한 id(결과의 출처), @p order 는 도는 순서입니다. */
#define SW_EDITOR_MAP_CHECK( name, pID, order, pfnCheck ) \
    SW_EDITOR_REGISTER( ::sw::editor::EditorMapCheckRegistration, MapCheck_##name, { pID, order }, pfnCheck )
