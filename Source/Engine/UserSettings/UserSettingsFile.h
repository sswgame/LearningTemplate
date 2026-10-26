/**
 * @file UserSettingsFile.h
 * @brief 사용자 파일(`usersettings.json`)의 형식입니다. JSON 글과 키 · 값 목록을 오가고, 이전 버전 파일을 스키마의 `<Upgrade>` 단계로 올립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct UserSettingDef;

    class UserSettingsSchema;

    /** @brief 사용자 파일에서 읽은 키와 값 한 줄입니다. 키는 플레이어 파일의 글이라 스키마에 없을 수 있다(관리자가 경고하고 버린다). */
    struct UserSettingsFileEntry
    {
        string _key;
        string _value;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 사용자 파일에 쓸 값 한 줄입니다. 키는 스키마의 설정 정의 자체라 모르는 키를 만들 수 없습니다.
     * @details 읽기는 플레이어 데이터(모르는 키는 경고 후 버림)이고 쓰기는 프로그램이 만든 목록이다 — 쓰기의 모르는 키는 결함이므로 글 키 대신 정의를 받아
     *          그 경우를 형식에서 없앴다(글 키를 받던 때는 String 타입으로 보고 그대로 썼다).
     */
    struct UserSettingsWriteEntry
    {
        const UserSettingDef& _def;
        string                _value;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UserSettingsFile
     * @brief 사용자 파일의 글을 읽고 씁니다. 값의 검사 · 기본값과의 비교는 `UserSettingsManager` 가 합니다.
     * @details 형식은 `{ "version": N, "values": { "<설정 id>": 값 } }` 입니다. 사용자 파일은 배포된 플레이어 데이터라 이전 버전을 읽어야 합니다
     *          (게임 데이터와 달리 다시 쓸 수 없다).
     */
    struct SW_API UserSettingsFile
    {
        static constexpr utf8 kFileName[] = "usersettings.json";

        /**
         * @brief JSON 글을 키 · 값 목록으로 읽고, 파일 버전부터 스키마 버전까지 버전 올리기 단계를 적힌 순서대로 적용합니다.
         * @return JSON 이 아니거나 `values` 오브젝트가 없으면 경고를 남기고 false 입니다. 스키마보다 새 버전이면 경고만 남깁니다.
         * @details 숫자는 쓰인 그대로, 불리언은 `true` / `false` 글이 됩니다. null · 배열 · 오브젝트는 빈 글입니다.
         */
        [[nodiscard]] static bool parseJSON( string_view jsonText, string_view sourceName, const UserSettingsSchema& schema, vector<UserSettingsFileEntry>& outListEntry );
        /** @brief 스키마 버전과 @p listEntry 를 JSON 글로 만듭니다. 불리언 · 숫자 설정은 JSON 불리언 · 숫자로 쓰고(숫자로 읽히지 않으면 글), 나머지는 글로 씁니다. */
        static string makeJSON( const UserSettingsSchema& schema, const vector<UserSettingsWriteEntry>& listEntry );
    };
} // namespace sw
