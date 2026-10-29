/**
 * @file DataTableModel.h
 * @brief 데이터 테이블 창의 모델 — 로컬라이즈 프로젝트 목록 · 고른 프로젝트 · 레코드 표(키 더하기 · 지우기 · 걸러 보기)와 게임 데이터 XML 파일 목록 · 고른 파일의 원문 · 표 문서,
 *        두 문서의 dirty 와 읽기 · 저장 · 되돌리기, 백그라운드 읽기 잡. ImGui 를 모릅니다.
 * @details 패널(`DataTablePanel`)은 툴바 · 표 · 글 상자를 그리고 입력란 글 · 원문 보기 토글 · 표 편집기(`CatalogTableEditor`)만 든다 — 계층 · 인스펙터 `Model/` 과 같은 선이다
 *          (언리얼 데이터 테이블 에디터의 툴킷 상태와 Slate 행 위젯이 나뉜 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorDataTableCommands.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

namespace sw::editor
{
    class EditorListFilter;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class DataTableModel
     * @brief 데이터 테이블 창의 상태와 명령입니다. 파일 머리말 참고.
     * @details 두 문서(로컬라이즈 · 게임 데이터)를 한 창이 들기 때문에 dirty 비트가 둘이다 — 저장 · 되돌리기는 dirty 한 쪽만 한다.
     */
    class DataTableModel
    {
    public:
        DataTableModel();

        /** @brief 백그라운드 읽기 잡의 결과를 받고, 아직 읽지 않은 문서는 읽기를 겁니다. 패널이 그리기마다 부릅니다. */
        void pollBackgroundJobs();

        /** @brief dirty 한 문서만 저장합니다. 둘 다 깨끗해졌으면 true 입니다. */
        [[nodiscard]] bool save();
        /** @brief dirty 한 문서만 디스크의 내용으로 되돌립니다. */
        void revert();
        bool isLocalizationDirty() const { return _bLocalizationDirty; }
        bool isGameDataDirty() const { return _bGameDataDirty; }
        bool isAnyDirty() const { return _bLocalizationDirty || _bGameDataDirty; }

        // ------------------------------------------------------------------------------
        // 로컬라이즈
        // ------------------------------------------------------------------------------
        bool                  isLocalizationLoaded() const { return _bLocalizationLoaded; }
        const vector<string>& getLocalizationProjects() const { return _listLocalizationProject; }
        int32                 getSelectedProjectIndex() const { return _selectedProjectIndex; }
        /** @brief 프로젝트 @p projectIndex 로 바꾸고 다시 읽습니다. 고친 것이 남았으면(잃지 않게) 바꾸지 않습니다. */
        void selectProject( size_t projectIndex );
        /** @brief 레코드 표입니다. 칸을 고친 쪽은 `markRecordModified` 를 부릅니다. */
        LocalizationSheet& editSheet() { return _localizationSheet; }
        /** @brief 레코드 @p record 를 고친 것으로 표시하고 로컬라이즈 문서를 dirty 로 만듭니다(칸 편집). */
        void markRecordModified( LocalizationRecord& record );
        /**
         * @brief 키 @p key 의 빈 레코드를 더합니다(Add Key 단추). 키가 비었거나 테이블이 없거나 이미 있으면 더하지 않습니다.
         * @return 더했으면 true 입니다(입력란을 비운다).
         */
        [[nodiscard]] bool addKey( string_view key );
        /** @brief 레코드 @p recordIndex 를 지우고 그 키를 지운 키로 남깁니다(저장이 테이블에서 뺀다). */
        void deleteRecord( size_t recordIndex );
        /** @brief 필터를 지난 레코드 순번을 모읍니다(@p outListVisibleIndex 는 호출한 쪽이 재사용하는 버퍼). */
        void collectVisibleRecords( const EditorListFilter& filter, vector<size_t>& outListVisibleIndex ) const;
        /** @brief 고친 것을 버리고 다음 `pollBackgroundJobs` 가 프로젝트를 다시 읽게 합니다(Reload 단추). */
        void reloadLocalization();
        /** @brief 로컬라이즈 프로젝트를 저장합니다. 하나라도 쓰지 못했으면 dirty 를 지우지 않는다 — 저장 확인이 다시 묻는다. */
        void saveLocalization();

        // ------------------------------------------------------------------------------
        // 게임 데이터 XML
        // ------------------------------------------------------------------------------
        const vector<GameDataFileEntry>& getGameDataFiles() const { return _listGameDataFile; }
        int32                            getSelectedGameDataIndex() const { return _selectedGameDataIndex; }
        /** @brief 고른 파일 항목입니다. 고르지 않았으면 nullptr 입니다. */
        const GameDataFileEntry* findSelectedGameDataFile() const;
        /** @brief 파일 @p fileIndex 를 골라 읽습니다. 다른 파일의 고친 것이 남았으면(잃지 않게) 바꾸지 않습니다(같은 파일은 다시 읽는다). */
        void selectGameDataFile( size_t fileIndex );
        /** @brief 고른 파일을 디스크에서 다시 읽습니다(Revert 단추). 읽지 못하면 고름을 풉니다. */
        void loadSelectedGameDataFile();
        /** @brief 고른 파일에 원문을 씁니다(Save File 단추). */
        void saveSelectedGameDataFile();
        /** @brief 다음 `pollBackgroundJobs` 가 파일 목록을 다시 읽게 합니다(Refresh Files 단추). */
        void                   reloadGameDataFiles();
        const string&          getGameDataRawText() const { return _selectedGameDataRawText; }
        const CatalogDocument& getGameDataDocument() const { return _gameDataDocument; }
        /** @brief 표 편집기가 직접 고치는 문서입니다. 고쳤으면 `commitGameDataTableEdit` 을 부릅니다. */
        CatalogDocument& editGameDataDocument() { return _gameDataDocument; }
        /** @brief 원문 글 상자가 고친 글 @p text 로 문서를 바꾸고 dirty 로 만듭니다. */
        void replaceGameDataRawText( string_view text );
        /** @brief 표 편집기가 문서를 고친 뒤 원문을 문서 글로 맞추고 dirty 로 만듭니다. */
        void commitGameDataTableEdit();
        /** @brief 게임 데이터 문서를 새로 읽은(또는 되돌린) 횟수입니다. 늘면 패널이 표 편집기 상태를 지운다. */
        uint32 getGameDataLoadCount() const { return _gameDataLoadCount; }

    private:
        /** @brief 원문 @p text 를 고른 파일의 저장된 내용으로 두고 문서를 다시 엽니다(깨진 원문도 글 상자로 고치게 열린다). */
        void openGameDataText( string&& text );

        LocalizationSheet         _localizationSheet;
        vector<string>            _listLocalizationProject;
        vector<GameDataFileEntry> _listGameDataFile;
        string                    _selectedGameDataRawText;
        string                    _savedGameDataRawText;
        CatalogDocument           _gameDataDocument;
        EditorLocalizationLoadJob _localizationJob;
        EditorGameDataScanJob     _gameDataJob;
        int32                     _selectedGameDataIndex;
        int32                     _selectedProjectIndex;
        uint32                    _gameDataLoadCount;
        bool                      _bLocalizationLoaded;
        bool                      _bGameDataLoaded;
        bool                      _bLocalizationDirty;
        bool                      _bGameDataDirty;
    };
} // namespace sw::editor
