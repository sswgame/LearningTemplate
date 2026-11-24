/**
 * @file CharacterEditorLocalStore.h
 * @brief 캐릭터 편집 창의 사용자 로컬 저장소(외형 프리셋 · 썸네일 슬롯) 주인입니다 - 에디터 확장 서브시스템(`SW_EDITOR_SUBSYSTEM`)이 `LocalStoreChannel` 을 든다.
 * @details 에디터는 게임 인스턴스 밖이라 게임 범위의 `GameLocalStore` 를 쓸 수 없다. 사용자가 만든 프리셋은 콘텐츠가 아니라 사용자 데이터이므로 콘텐츠 트리에 쓰지 않고
 *          활성 게임의 사용자 데이터 폴더 아래 `editor` 에 슬롯으로 둔다(언리얼은 에디터 사용자 설정을 `Saved/` 에, 유니티는 `EditorPrefs` 에 둔다).
 *          봉인하지 않는다(사람이 열어 볼 수 있는 편집 자료 - 암호 제공자를 받지 않는다). 완료는 에디터 프레임 단계가 프레임마다 넘긴다.
 *          다른 에디터 확장도 로컬 슬롯이 필요해지면 이 주인을 공용 에디터 플러그인으로 옮긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorSubsystem.h"

#include "GameFramework/Runtime/Foundation/Save/Local/LocalStoreChannel.h"

namespace sw
{
    class UserAppearancePresetStore;
} // namespace sw

namespace sw::editor
{
    /**
     * @class CharacterEditorLocalStore
     * @brief 에디터 범위 서브시스템입니다(`editor::findSubsystem<CharacterEditorLocalStore>()`). UI 스레드에서만 쓴다.
     */
    class CharacterEditorLocalStore final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "CharacterEditorLocalStore";
        /** @brief 저장소 자리 - 활성 게임의 사용자 데이터 폴더 아래 상대 경로입니다. */
        static constexpr const utf8* kDirectory = "editor";
        /** @brief 사용자 외형 프리셋 목록(`UserAppearancePresetStore` 본문) 슬롯입니다. */
        static constexpr const utf8* kPresetSlot = "charactereditor/presets";

        /** @brief 파일 백엔드를 엽니다. 열지 못하면 오류를 남기고 실패한다(프리셋 줄이 상태 줄로 알린다). */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 남은 쓰기를 끝까지 마치고 닫습니다. */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }
        void        resetToFreshEditor() override {} // 디스크 칸으로 가는 통로라 단계 상태가 없다

        LocalStoreChannel& getChannel() { return _channel; }

        /**
         * @brief 프리셋 @p presetName 의 썸네일 슬롯 이름입니다 — 프리셋이 이미 든 슬롯이면 그것, 아니면 @p store 의 어느 프리셋도 들지 않은 가장 작은 번호(`charactereditor/thumb.<번호>`).
         * @details 슬롯 이름은 프리셋 줄(`UserAppearancePresetRecord::_thumbnailSlot`)에 적히므로 이름에서 만들 까닭이 없다 — 번호로 주면 대소문자 · 규칙 밖 글자 · 길이가 달라도 두 프리셋이 한 슬롯을 나누지 않는다
         *          (언리얼 에셋이 경로가 아니라 발급한 id 로 썸네일을 잇는 것과 같은 선).
         */
        [[nodiscard]] static string makeThumbnailSlot( const UserAppearancePresetStore& store, string_view presetName );

    private:
        LocalStoreChannel _channel;
    };
} // namespace sw::editor
