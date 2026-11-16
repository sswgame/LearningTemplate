/**
 * @file UserSettingsFileEntry.h
 * @brief 사용자 파일(`usersettings.json`)의 형식입니다. JSON 글과 키 · 값 목록을 오가고, 이전 판 파일을 스키마 주인별 `<Upgrade>` 단계로 올립니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

namespace sw
{
    struct UserSettingsWriteEntry;
    struct UserSettingDef;
    struct UserSettingsFileContent;

    class UserSettingsSchema;

    /** @brief 사용자 파일 읽기 결과입니다. 셋 다 부르는 쪽이 다르게 다뤄야 해서 bool 이 아니다. */
    enum class UserSettingsFileReadResult : uint8
    {
        Read,           ///< 읽고 이 빌드의 판까지 올렸습니다.
        Unreadable,     ///< JSON 이 아니거나 형식이 틀렸습니다(부르는 쪽이 `.corrupt` 로 치운다).
        NewerThanBuild, ///< 어느 주인의 판이 이 빌드보다 새 파일입니다. 내용을 쓰지 않는다(옛 번호로 다시 쓰면 새 빌드가 올림을 또 돌린다).
    };

    /** @brief 사용자 파일에서 읽은 키와 값 한 줄입니다. 키는 플레이어 파일의 글이라 스키마에 없을 수 있다(관리자가 원문 그대로 보관해 다시 쓴다). */
    struct SW_API UserSettingsFileEntry
    {
        string _key;
        string _value;
        string _rawJSON; ///< 파일에 적힌 값의 JSON 원문입니다. 올림 단계가 값을 바꾸면 비운다(그때는 `_value` 가 정본).

        // 사용자 파일의 글을 읽고 씁니다. 값의 검사 · 기본값과의 비교는 `UserSettingsManager` 가 합니다.
        // 형식은 `{ "versions": { "<주인>": N }, "values": { "<설정 id>": 값 } }` 입니다. 사용자 파일은 배포된 플레이어 데이터라 이전 판을 읽어야 합니다
        // (판 번호 하나 `"version": N` 를 쓰던 옛 파일 포함).
        static constexpr utf8 kFileName[] = "usersettings.json";

        /**
         * @brief JSON 글을 읽고, 스키마 주인마다 파일의 판부터 스키마 판까지 그 주인의 올림 단계를 적힌 순서대로 적용합니다.
         * @details 숫자는 쓰인 그대로, 불리언은 `true` / `false` 글이 됩니다. null · 배열 · 오브젝트는 빈 글입니다(원문은 `_rawJSON`).
         *          파일에 없는 주인은 판 0 이다. 이 빌드에 없는 주인의 판은 @p outContent 에 그대로 담아 다시 쓴다.
         */
        [[nodiscard]] static UserSettingsFileReadResult parseJSON( string_view jsonText, string_view sourceName, const UserSettingsSchema& schema,
                                                                   UserSettingsFileContent& outContent );
        /**
         * @brief 스키마 주인별 판과 @p listEntry 를 JSON 글로 만듭니다. 불리언 · 숫자 설정은 JSON 불리언 · 숫자로 쓰고(숫자로 읽히지 않으면 글), 나머지는 글로 씁니다.
         * @param carried 읽은 파일에서 이 빌드가 모르는 키 · 주인입니다. 원문 그대로 다시 쓴다(다른 구성 · 다른 판의 빌드가 쓴 값을 지우지 않는다).
         * @return 값에 잘못된 UTF-8 이 있으면 오류입니다(`JSONDocument::dump`) — 부르는 쪽은 있던 파일을 덮지 않는다.
         */
        [[nodiscard]] static expected<string> makeJSON( const UserSettingsSchema& schema, const vector<UserSettingsWriteEntry>& listEntry,
                                                        const UserSettingsFileContent& carried );
    };

    /** @brief 파일에 적힌 판 번호의 주인 하나와 그 판입니다. */
    struct UserSettingsFileOwnerVersion
    {
        hashed_string _owner;
        uint32        _version{ 0 };
    };

    /** @brief 사용자 파일 하나의 내용입니다. 읽기 결과이자, 다시 쓸 때 원문 그대로 보관할 몫(모르는 키 · 주인)입니다. */
    struct UserSettingsFileContent
    {
        vector<UserSettingsFileEntry>        _listEntry;
        vector<UserSettingsFileOwnerVersion> _listForeignOwner; ///< 이 빌드의 스키마에 없는 주인의 판입니다(예: Shipping 이 읽은 Dev 의 `debughud`).
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
