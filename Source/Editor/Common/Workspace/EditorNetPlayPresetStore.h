/**
 * @file EditorNetPlayPresetStore.h
 * @brief 멀티 프로세스 재생의 사용자 프리셋(이름 붙인 `EditorNetPlaySettings`)을 사용자 파일 하나에 저장 · 고르기 · 지우기를 합니다. ImGui 에 의존하지 않습니다.
 * @details 파일은 `Saved/Editor/NetPlayPresets.json` 이고 커밋하지 않는다(결정 기록 D7-3.3). 바꾸는 호출은 즉시 파일을 다시 쓴다 — 에디터가 죽어도
 *          마지막으로 고른 프리셋이 남는다. 깨진 파일은 환경설정 · 단축키와 같은 한 경로(`EditorUserFile::quarantineCorruptFile`)로 치운다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Config/EditorUserFile.h"
#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/EditorNetPlaySettings.h"

namespace sw::editor
{
    /**
     * @class EditorNetPlayPresetStore
     * @brief 프리셋 목록과 마지막으로 고른 이름을 들고, 바꿀 때마다 `load` 로 연 파일에 씁니다.
     */
    class SW_EDITOR_API EditorNetPlayPresetStore
    {
    public:
        /** @brief 저장 파일 이름입니다(에디터 상태 폴더 `Saved/Editor/`). */
        static constexpr const utf8* kFileName = "NetPlayPresets.json";

        EditorNetPlayPresetStore();

        /**
         * @brief @p filePath 를 읽고, 이 뒤로 바꾸는 호출이 쓸 파일로 기억합니다. 읽기 전의 목록은 버립니다.
         * @return 없으면 `Missing`(빈 목록). 형식이 틀렸거나 칸이 빠졌거나 이름이 비었거나 겹치거나 고른 이름이 목록에 없으면 `.corrupt` 로 치우고
         *         오류를 남긴 뒤 빈 파일을 새로 쓰고 `Recovered`(`EditorUserFile`). 깨진 파일의 일부 값을 쓰지 않는다.
         */
        [[nodiscard]] EditorUserFileResult load( string_view filePath );
        /**
         * @brief @p name 으로 @p settings 를 저장하고(같은 이름이 있으면 덮는다) 그 프리셋을 고른 것으로 둔 뒤 파일을 씁니다.
         * @return 이름이 비었거나, `load` 하지 않았거나, 파일을 쓰지 못하면 false 입니다(이유는 오류로 남긴다).
         */
        [[nodiscard]] bool savePreset( string_view name, const EditorNetPlaySettings& settings );
        /** @brief @p name 을 고른 것으로 두고 파일을 씁니다. 없는 이름이거나 쓰지 못하면 false 이고 고른 것은 그대로입니다(바꾸는 호출은 모두 그렇다). */
        [[nodiscard]] bool selectPreset( string_view name );
        /** @brief @p name 을 지우고 파일을 씁니다. 고른 프리셋이었으면 고른 것도 비웁니다. 없는 이름이거나 쓰지 못하면 false 입니다. */
        [[nodiscard]] bool removePreset( string_view name );

        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        const EditorNetPlaySettings* findPreset( string_view name ) const;
        /** @brief 고른 프리셋의 구성입니다. 고른 것이 없으면 nullptr 입니다. */
        const EditorNetPlaySettings*       findSelectedPreset() const { return findPreset( _file._selectedPresetName ); }
        const vector<EditorNetPlayPreset>& getPresets() const { return _file._listPreset; }
        const string&                      getSelectedPresetName() const { return _file._selectedPresetName; }

        /** @brief 에디터가 쓰는 저장 파일 경로입니다(에디터 상태 폴더의 `kFileName`). */
        static string getDefaultFilePath();

    private:
        /** @brief 이름이 비지 않고 겹치지 않으며 고른 이름이 목록에 있으면 true 입니다. 아니면 @p outReason 에 첫 이유를 적습니다. */
        [[nodiscard]] static bool isConsistent( const EditorNetPlayPresetFile& file, string& outReason );
        /** @brief 이름의 인덱스입니다. 없으면 `invalid_index::kUint32` 입니다. */
        uint32 findPresetIndex( string_view name ) const;
        /** @brief @p file 을 `_filePath` 에 쓰고, 썼을 때만 지금 목록으로 들입니다 — 쓰지 못한 변경이 메모리에만 남아 파일과 어긋나지 않게 합니다. */
        [[nodiscard]] bool commitFile( EditorNetPlayPresetFile&& file );

        EditorNetPlayPresetFile _file;
        string                  _filePath; ///< `load` 가 연 파일. 비어 있으면 바꾸는 호출이 실패한다
    };
} // namespace sw::editor
