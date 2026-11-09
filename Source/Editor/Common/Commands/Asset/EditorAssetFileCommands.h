/**
 * @file EditorAssetFileCommands.h
 * @brief 콘텐츠 브라우저의 에셋 파일 관리입니다. 새 폴더와 새 에셋, 이름 바꾸기와 옮기기(참조 고침), 복제, 임포트, 휴지통 삭제, 탐색기에서 보기를 맡습니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorAssetMove.h"

namespace sw::editor
{
    class EditorReferenceIndex;

    struct EditorNewAssetTemplate;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorAssetFileCommands
     * @brief 에셋 파일을 만들고 옮기고 지웁니다. 이름은 리소스 규칙(소문자, 숫자, `_`, `-`, `.`)을 지키고, 겹치면 `_2` · `_3` 을 붙입니다.
     * @details 옮기기와 이름 바꾸기는 참조 역색인(`EditorReferenceIndex`)이 아는 텍스트 에셋의 글을 새 리소스 id 로 고칩니다(언리얼 Fix Up Redirectors 를 바로 하는 것과 같다).
     *          에셋은 경로 하나로 가리키므로(식별자 표 · `.meta` 는 없다) 참조를 고치는 일이 옮기기의 전부다. 고치는 자리는 경로 칸(`AssetReferenceFields` —
     *          커밋 전 검사 · `py -3 -m Scripts move-asset` 과 같은 `Config/Dev/AssetReferenceFields.json`)의 값뿐이다.
     */
    struct EditorAssetFileCommands
    {
        /** @brief 리소스 이름 규칙에 맞는 이름이면 true 입니다(비어 있지 않고 `[a-z0-9_.-]` 만, `.` 로 시작하지 않는다). */
        static bool isValidAssetName( string_view name );
        /** @brief 파일 이름을 줄기와 붙은 확장자로 나눕니다. 확장자는 첫 `.` 부터입니다(`a.scene.xml` → `a`, `.scene.xml`). */
        static void splitAssetName( string_view fileName, string& outStem, string& outSuffix );
        /** @brief @p folderAbs 안에 `<stem><suffix>`(있으면 `<stem>_2<suffix>` 부터)를 만들어 자리를 잡고 그 경로를 돌려줍니다(`FileUtil::reserveUniquePath`, 접미사가 비면 폴더). 못 잡으면 빈 글입니다. */
        static string makeUniquePath( string_view folderAbs, string_view stem, string_view suffix );
        /**
         * @brief 글 안의 @p oldID 를 @p newID 로 바꿉니다. 경로 글자(`[a-z0-9_./-]`)가 바로 앞뒤에 붙은 곳은 바꾸지 않습니다(`a.material` 이 `aa.material` 안에서 바뀌지 않게).
         * @return 바꾼 수입니다. 대소문자는 가리지 않고 찾는다.
         */
        static uint32 replaceReference( string& inoutText, string_view oldID, string_view newID );

        /** @brief @p parentAbs 안에 `new_folder` 폴더를 만듭니다. */
        [[nodiscard]] static bool createFolder( string_view parentAbs, string& outFolderAbs );
        /** @brief @p folderAbs 안에 @p assetTemplate(종류 등록의 틀, `EditorAssetTypeRegistry::collectNewAssetTemplates`)의 새 에셋(`<줄기><확장자>`, 겹치면 `_2` 부터)을 씁니다. */
        [[nodiscard]] static bool createAsset( string_view folderAbs, const EditorNewAssetTemplate& assetTemplate, string& outAssetAbs );
        /** @brief 에셋을 같은 폴더에 `<stem>_2` 처럼 복제합니다. */
        [[nodiscard]] static bool duplicateAsset( string_view sourceAbs, string& outAssetAbs );
        /**
         * @brief 에셋을 @p destinationAbs 로 옮기고(이름 바꾸기 포함) 그 에셋을 적은 텍스트 에셋의 경로 칸을 고칩니다(칸 목록을 읽지 못하면 옮기지 않는다).
         * @param inoutIndex 다 만들어진 역색인. 옮긴 파일과 고친 파일의 줄을 다시 훑는다.
         * @param outFixedFileCount 글을 고친 파일 수
         * @return 대상이 이미 있거나, 이름이 규칙에 맞지 않거나, 역색인이 아직이면 아무것도 하지 않고 false 입니다(이유를 알린다).
         */
        [[nodiscard]] static bool moveAsset( string_view sourceAbs, string_view destinationAbs, EditorReferenceIndex& inoutIndex, uint32& outFixedFileCount );
        /**
         * @brief 옮기기 묶음을 디스크를 건드리지 않고 계획합니다 — 이름 규칙 · 겹침 · 역색인 · 경로 칸 목록을 확인하고, 고칠 참조 파일과 고칠 수 없는 참조(칸 밖)를 모은다.
         * @return 그대로 적용할 수 있으면 true. false 면 `_failure` 에 이유가 있다(고칠 수 없는 참조가 하나라도 있으면 false — 반쯤 고친 상태를 만들지 않는다).
         */
        [[nodiscard]] static bool planAssetMoves( const vector<EditorAssetMove>& listMove, const EditorReferenceIndex& index, EditorAssetMovePlan& outPlan );
        /**
         * @brief 옮기기 묶음을 트랜잭션으로 적용합니다. 계획을 다시 세우고, 고칠 글을 모두 미리 만든 뒤 파일을 옮기고 글을 쓴다.
         * @details 옮기기나 쓰기가 하나라도 지면(잠긴 파일 · 읽기 전용) 쓴 글을 원래대로, 옮긴 파일을 거꾸로 되돌린다. 되돌리지 못한 파일은 이름을 오류 로그와 알림에 남긴다.
         *          언리얼의 에셋 이동이 리다이렉터를 남기는 자리에서, 이 엔진은 경로 하나로 가리키므로 참조 글을 같은 트랜잭션에서 고친다.
         * @return 다 옮기고 고쳤으면 true(`_fixedFileCount`). false 면 디스크는 시작 상태이거나, 아니면 남은 목록이 로그에 있다.
         */
        [[nodiscard]] static bool applyAssetMoves( const vector<EditorAssetMove>& listMove, EditorReferenceIndex& inoutIndex, EditorAssetMovePlan& outPlan );
        /**
         * @brief 폴더 @p sourceFolderAbs 를 @p destinationParentAbs 아래로 옮기는 파일 옮기기 목록을 만듭니다(폴더 안 모든 파일, 하위 폴더 구조 그대로).
         * @return 옮기기 목록입니다. 폴더가 아니거나 · 자기 안으로 옮기기 · 이미 있는 대상 · 빈 폴더면 그 이유입니다. 옮긴 뒤 빈 원본 폴더 지우기는 부른 쪽이 한다.
         */
        [[nodiscard]] static expected<vector<EditorAssetMove>> collectFolderMoves( string_view sourceFolderAbs, string_view destinationParentAbs );
        /** @brief 에셋을 OS 휴지통으로 보냅니다. 휴지통이 없는 플랫폼은 지웁니다. */
        [[nodiscard]] static bool moveToTrash( string_view absolutePath );
        /** @brief 에셋 파일을 삭제합니다. */
        [[nodiscard]] static bool deleteAsset( string_view absolutePath );
        /**
         * @brief 파일을 Content Browser 폴더로 복사합니다. 같은 이름이 있으면 덮지 않고 `_2` 처럼 새 이름으로 둡니다.
         * @return 복사한 파일 수입니다. 이미 그 폴더에 있는 파일과 없는 파일은 건너뜁니다.
         */
        static uint32 importFiles( string_view destFolderAbs, const vector<string>& listSourcePath );
        /**
         * @brief 콘솔 명령의 원본 파일 인자를 읽습니다 - 절대 경로면 그 디스크 파일, 아니면 리소스 id(`engine/automation/…`)의 파일이다. 둘 중 무엇인지는 글의 모양(엄격한 절대 경로인지)이 정한다.
         * @details 예전에는 리소스로 찾아보고 없으면 작업 폴더 상대 경로로 다시 풀었다(같은 글이 작업 폴더에 따라 다른 파일). `scene.open` · `asset.import` 가 같은 입구로 읽는다.
         */
        [[nodiscard]] static bool readSourceFileArgument( const string& argument, string& outAbsolutePath, string& outReply );

        /**
         * @brief 파일 탐색기에서 이 경로를 선택한 채로 엽니다.
         * @details 셸 없이 열도록 `Process::showInFileManager` 로 부른다. 셸(`system`)을 거치면 경로 안의 `&` 와 `"` 가 명령으로 해석되고 콘솔 창이
         *          한 번 깜빡인다.
         */
        static bool showInFileExplorer( string_view absolutePath );
        /** @brief `showInFileExplorer` 가 실행할 명령줄입니다(경로가 비면 빈 문자열). 실행 없이 인자를 단언할 수 있게 따로 둔다. */
        [[nodiscard]] static string makeFileExplorerCommand( string_view absolutePath );
        /**
         * @brief true 면 `showInFileExplorer` 가 명령을 기록만 하고 프로세스를 띄우지 않습니다(자동화 시나리오가 `explorer.dryRun` 으로 건다).
         * @details 탐색기를 실제로 띄우면 자동 실행이 창을 남기므로, 호출 직전까지(명령줄 조립 · 횟수)만 단언하려는 장치다.
         */
        static void setFileExplorerDryRun( bool bDryRun );
        /** @brief `showInFileExplorer` 가 명령을 만든 횟수입니다(드라이런 포함). */
        [[nodiscard]] static uint32 getFileExplorerLaunchCount();
        /** @brief 마지막으로 만든 탐색기 명령줄입니다. */
        [[nodiscard]] static string getLastFileExplorerCommand();
    };
} // namespace sw::editor
