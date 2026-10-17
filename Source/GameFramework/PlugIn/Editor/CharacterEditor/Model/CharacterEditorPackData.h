/**
 * @file CharacterEditorPackData.h
 * @brief 캐릭터 편집 창이 여는 게임 데이터(소켓 · 스켈레톤 · 레퍼런스 포즈 · 아이템 · 외형 폴더)의 경로 표입니다. 게임 팩의 `data/charactereditor.xml` 이 정본입니다.
 * @details 프레임워크의 에디터 확장은 게임 이름을 모른다. 활성 게임의 팩 루트(`GameConfig::getActive()._packRoot`)와 데이터 종류 `kDataKind` 로
 *          파일을 찾고(`GamePackDataUtil::tryMakeKindPath`), 파일이 없는 게임은 창이 "캐릭터 편집 데이터 없음" 을 보인다(다른 게임의 경로로 대체하지 않는다).
 *          네 칸은 모두 필수다 — 하나라도 빠지면 읽기가 실패한다. 레퍼런스 포즈는 적지 않고 스켈레톤 경로에서 만든다(창이 저장하는 짝 파일이라 아직 없을 수 있다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class XMLNode;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct CharacterEditorPackData
     * @brief `<CharacterEditor sockets="…" skeleton="…" items="…" appearance="…"/>` 한 줄입니다. 값은 도메인을 포함한 리소스 경로입니다.
     */
    struct CharacterEditorPackData
    {
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "charactereditor";

        string _socketPath{};       ///< 소켓 파일(`*.sockets.xml`)
        string _skeletonPath{};     ///< 스켈레톤(`*.skeleton.json`)
        string _refPosePath{};      ///< 레퍼런스 포즈(`*.refpose.xml`) — 스켈레톤 경로에서 만든다(데이터 칸 아님)
        string _itemsPath{};        ///< 장비 아이템 카탈로그(`items.xml`)
        string _appearanceFolder{}; ///< 외형 데이터 폴더(`data/appearance`)

        /** @brief 리소스 경로의 파일을 읽어 표를 바꿉니다. 파일이 없거나, 모르는 원소 · 속성이 있거나, 칸이 빠지면 false 이고 표는 비웁니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief `loadFromResource` 의 XML 글 버전입니다(테스트). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw::editor
