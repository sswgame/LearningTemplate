/**
 * @file CharacterEditorPackData.h
 * @brief 캐릭터 편집 창이 여는 게임 데이터(소켓 · 스켈레톤 · 레퍼런스 포즈 · 아이템 · 외형 폴더)의 경로 표입니다. 게임 팩의 `data/charactereditor.xml` 이 기준입니다.
 * @details 프레임워크의 에디터 확장은 게임 이름을 모른다. 활성 게임의 팩 루트(`editor::getSubsystem<const GameConfig>()._packRoot.get()`)와 데이터 종류 `kDataKind` 로
 *          파일을 찾고(`GamePackDataUtil::tryMakeKindPath`), 파일이 없는 게임은 창이 "캐릭터 편집 데이터 없음" 을 보인다(다른 게임의 경로로 대체하지 않는다).
 *          여섯 칸은 모두 필수다 — 하나라도 빠지면 읽기가 실패한다. 레퍼런스 포즈는 적지 않고 스켈레톤 경로에서 만든다(창이 저장하는 짝 파일이라 아직 없을 수 있다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    class XMLNode;
} // namespace sw

namespace sw::editor
{
    /**
     * @struct CharacterEditorPackData
     * @brief `<CharacterEditor sockets="…" skeleton="…" items="…" appearance="…" clipFolder="…" previewPreset="…" [rig="…"]/>` 한 줄입니다(`rig` 만 선택). 경로 값은 도메인을 포함한 리소스 경로이고 `previewPreset` 은 프리셋 id 입니다.
     */
    struct CharacterEditorPackData
    {
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "charactereditor";

        string _socketPath{};       ///< 소켓 파일(`*.sockets.xml`)
        string _skeletonPath{};     ///< 스켈레톤(`*.skeleton.xml`)
        string _refPosePath{};      ///< 레퍼런스 포즈(`*.refpose.xml`) — 스켈레톤 경로에서 만든다(데이터 칸 아님)
        string _itemsPath{};        ///< 장비 아이템 카탈로그(`items.xml`)
        string _appearanceFolder{}; ///< 외형 데이터 폴더(`data/appearance`)
        string _clipFolder{};       ///< 미리보기 몸이 재생하는 애니메이션 클립 폴더(`<이름>.animclip` 파일이 든 폴더 — 임포트가 스켈레톤 옆 `clips/` 에 쓴다)
        /**
         * @brief 미리보기 몸의 외형 프리셋 id(`presets.xml` 의 것)입니다. 외형 선택에 기준 프리셋이 없을 때 창의 미리보기 씬이 이 프리셋 위에 선택을 입힌다.
         * @details 몸은 프리팹이 아니라 외형 조립(`CharacterAppearanceComponent::previewSelection`)으로 세운다 — 몸 메시 · 스켈레톤 · 소켓은 프리셋의 몸 아이템이 정한다.
         */
        string _previewPresetID{};
        /**
         * @brief 미리보기 몸에 얹어 볼 제약 리그(`*.rig.xml` — 스트라이드 워핑 · 양손 IK 같은 후처리)의 경로입니다. **선택 칸**: 비면 게임에 미리볼 리그가 없다는 뜻이고 창은 리그 단추를 그리지 않는다.
         * @details 여섯 칸이 필수인 것은 없으면 창이 일하지 못하기 때문이고, 리그는 없어도 창이 일하므로 선택이다(없다고 다른 게임의 리그를 쓰지 않는다).
         */
        string _rigPath{};

        /** @brief 리소스 경로의 파일을 읽어 표를 바꿉니다. 파일이 없거나, 모르는 원소 · 속성이 있거나, 칸이 빠지면 false 이고 표는 비웁니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief `loadFromAsset` 의 XML 글 버전입니다(테스트). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw::editor
