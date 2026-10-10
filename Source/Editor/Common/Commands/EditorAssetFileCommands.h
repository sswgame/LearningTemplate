/**
 * @file EditorAssetFileCommands.h
 * @brief 콘텐츠 브라우저의 에셋 파일 관리입니다. 새 폴더와 새 에셋, 이름 바꾸기와 옮기기(참조 고침), 복제, 휴지통 삭제를 맡습니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    class EditorReferenceIndex;

    /** @brief 콘텐츠 브라우저가 새로 만드는 에셋 종류입니다. */
    enum class EditorNewAssetKind : uint8
    {
        Material = 0,
        Scene,
        Prefab
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorAssetFileCommands
     * @brief 에셋 파일을 만들고 옮기고 지웁니다. 이름은 리소스 규칙(소문자, 숫자, `_`, `-`, `.`)을 지키고, 겹치면 `_2` · `_3` 을 붙입니다.
     * @details 옮기기와 이름 바꾸기는 참조 역색인(`EditorReferenceIndex`)이 아는 텍스트 에셋의 글을 새 리소스 id 로 고칩니다(언리얼 Fix Up Redirectors 를 바로 하는 것과 같다).
     *          짝 `.meta` 는 GUID 가 이어지도록 함께 옮기고 그 안의 `sourcePath` 도 고친다. 복제는 `.meta` 를 베끼지 않는다(새 에셋은 새 GUID 다).
     */
    struct EditorAssetFileCommands
    {
        /** @brief 리소스 이름 규칙에 맞는 이름이면 true 입니다(비어 있지 않고 `[a-z0-9_.-]` 만, `.` 로 시작하지 않는다). */
        static bool isValidAssetName( string_view name );
        /** @brief 파일 이름을 줄기와 붙은 확장자로 나눕니다. 확장자는 첫 `.` 부터입니다(`a.scene.xml` → `a`, `.scene.xml`). */
        static void splitAssetName( string_view fileName, string& outStem, string& outSuffix );
        /** @brief @p folderAbs 안에서 비어 있는 `<stem><suffix>` 경로입니다. 있으면 `<stem>_2<suffix>` 부터 셉니다. */
        static string makeUniquePath( string_view folderAbs, string_view stem, string_view suffix );
        /**
         * @brief 글 안의 @p oldID 를 @p newID 로 바꿉니다. 경로 글자(`[a-z0-9_./-]`)가 바로 앞뒤에 붙은 자리는 바꾸지 않습니다(`a.material` 이 `aa.material` 안에서 바뀌지 않게).
         * @return 바꾼 수입니다. 대소문자는 가리지 않고 찾는다.
         */
        static uint32 replaceReference( string& inoutText, string_view oldID, string_view newID );

        /** @brief @p parentAbs 안에 `new_folder` 폴더를 만듭니다. */
        [[nodiscard]] static bool createFolder( string_view parentAbs, string& outFolderAbs );
        /** @brief @p folderAbs 안에 새 에셋(`new_material.material` · `new_scene.scene.xml` · `new_prefab.prefab.xml`)을 씁니다. */
        [[nodiscard]] static bool createAsset( string_view folderAbs, EditorNewAssetKind kind, string& outAssetAbs );
        /** @brief 에셋을 같은 폴더에 `<stem>_2` 처럼 복제합니다(`.meta` 는 베끼지 않는다). */
        [[nodiscard]] static bool duplicateAsset( string_view sourceAbs, string& outAssetAbs );
        /**
         * @brief 에셋을 @p destinationAbs 로 옮기고(이름 바꾸기 포함) 그 에셋을 적은 텍스트 에셋의 글을 고칩니다.
         * @param inoutIndex 다 만들어진 역색인. 옮긴 파일과 고친 파일의 줄을 다시 훑는다.
         * @param outFixedFileCount 글을 고친 파일 수
         * @return 대상이 이미 있거나, 이름이 규칙에 맞지 않거나, 역색인이 아직이면 아무것도 하지 않고 false 입니다(이유를 알린다).
         */
        [[nodiscard]] static bool moveAsset( string_view sourceAbs, string_view destinationAbs, EditorReferenceIndex& inoutIndex, uint32& outFixedFileCount );
        /** @brief 에셋과 짝 `.meta` 를 OS 휴지통으로 보냅니다. 휴지통이 없는 플랫폼은 지웁니다. */
        [[nodiscard]] static bool moveToTrash( string_view absolutePath );
    };
} // namespace sw::editor
