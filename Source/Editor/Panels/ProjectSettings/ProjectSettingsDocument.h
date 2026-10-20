/**
 * @file ProjectSettingsDocument.h
 * @brief 프로젝트 설정 파일 하나(또는 XML 파일 안의 설정 요소 하나)의 편집 상태입니다. 리플렉션 인스턴스를 들고, 기본값과 다른 키만 씁니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Config/ConfigManager.h"

namespace sw
{
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ProjectSettingsDocument
     * @brief 설정 타입 하나의 현재 값과 기본값 인스턴스를 만들어 두고, 파일과 주고받습니다.
     * @details 인스턴스는 타입의 기본 생성자(`$ctor`)로 만든다 — 설정 타입마다 템플릿 코드를 두지 않고 색인이 알려 주는 이름만으로 연다.
     *          파일에는 기본값과 다른 값만 적는다(언리얼 `Default*.ini` 와 같다). 기본값을 다시 적은 줄은 코드의 기본값을 바꿔도 따라가지 않는 옛 값이 된다.
     *          읽기는 `ConfigManager::readConfigJSON` 으로 엄격하게 하므로 모르는 키나 범위 밖 값이 든 파일은 `Invalid` 이고, 그동안 저장을 막는다.
     *          요소 모드(`open` 의 요소 이름)는 XML 파일 안의 요소 하나(팩 `gamesettings.xml` 의 `<Engine>`)만 읽고 쓴다(`ConfigManager::readConfigXMLElement`).
     *          저장은 그 요소의 글만 바꾸고 파일의 나머지(다른 요소 · 주석)는 그대로 둔다. 요소 안의 주석은 저장하면 사라진다.
     */
    class ProjectSettingsDocument
    {
    public:
        ProjectSettingsDocument();
        ~ProjectSettingsDocument();
        ProjectSettingsDocument( const ProjectSettingsDocument& )            = delete;
        ProjectSettingsDocument& operator=( const ProjectSettingsDocument& ) = delete;

        /**
         * @brief @p type 의 인스턴스를 만들고 @p absolutePath 의 파일 값을 입힙니다. @p elementName 이 있으면 그 XML 파일 안의 그 요소 값입니다.
         * @return `Loaded` 면 파일 값을 입혔고, `Missing` 이면 파일(요소 모드는 요소)이 없어 기본값입니다. `Invalid` 면 파일이 틀렸거나 타입에 기본 생성자가 없어 열지 못했습니다.
         *         `Invalid` 일 때도 가능하면 기본값 인스턴스를 들고 있어, 창이 오류를 보이며 Reset 으로 고칠 수 있습니다.
         */
        [[nodiscard]] ConfigReadResult open( const TypeInfo& type, string_view absolutePath, string_view elementName = {} );
        /** @brief 인스턴스를 해제합니다. */
        void shutdown();

        /** @brief 파일을 다시 읽어 현재 값을 덮습니다(편집을 버린다). 결과는 `open` 과 같습니다. */
        [[nodiscard]] ConfigReadResult revert();
        /** @brief 현재 값을 모두 기본값으로 되돌립니다(파일은 그대로 — 저장해야 쓴다). 기본값 인스턴스가 없으면 아무것도 하지 않습니다. */
        void resetToDefault();
        /**
         * @brief 기본값과 다른 키만 파일에 씁니다. 읽지 못한 파일(`Invalid`)을 덮어쓰지 않도록, 그 상태에서는 false 입니다.
         * @details 저장하면 마지막으로 저장한 글을 바꿔 `isModified` 가 false 가 된다. 요소 모드는 그 요소만 바꾸고(없으면 루트 맨 앞에 넣는다),
         *          파일이 없거나 쓴 글을 다시 읽어 같은 값이 아니거나 필수 칸이 빠졌으면 쓰지 않고 false 다.
         */
        [[nodiscard]] bool save();

        /** @brief 열려 있으면 true 입니다(인스턴스가 있다). */
        [[nodiscard]] bool isOpen() const { return _pCurrent != nullptr; }
        /** @brief 마지막으로 읽거나 저장한 글과 지금 값의 차이 글이 다르면 true 입니다. */
        [[nodiscard]] bool isModified() const;
        /** @brief 읽지 못해(`Invalid`) 저장을 막고 있으면 true 입니다. */
        [[nodiscard]] bool isLoadFailed() const { return _readResult == ConfigReadResult::Invalid; }
        /** @brief 파일이 디스크에 있으면 true 입니다(없으면 기본값으로 시작했고 저장하면 만들어진다). */
        [[nodiscard]] bool hasFile() const { return _readResult == ConfigReadResult::Loaded; }

        /** @brief 지금 값에서 기본값과 다른 말단 키의 수입니다(구조체 칸은 안으로 들어가 센다). */
        [[nodiscard]] uint32 countNonDefaultKeys() const;
        /** @brief 파일에 쓸 글 — 기본값과 다른 키만 든 JSON(들여쓰기 4)이고, 요소 모드는 그 요소의 XML 입니다. 열려 있지 않으면 빈 글입니다. */
        [[nodiscard]] string makeDifferenceText() const;
        /** @brief 요소 모드(XML 파일 안의 설정 요소 하나)면 true 입니다. */
        [[nodiscard]] bool isElementMode() const { return _elementName.empty() == false; }
        /** @brief 타입이 바탕 층(`REFLECT( Meta = "ConfigBaseLayer" )` — 값이 파일에만 있다)이면 true 입니다. 그때 저장은 모든 키를 쓰고 되돌리기는 파일 값입니다. */
        [[nodiscard]] bool isBaseLayer() const;

        static constexpr const utf8* kBaseLayerMeta = "ConfigBaseLayer"; ///< 바탕 층 표시 메타

        /** @brief 편집할 현재 값입니다. 그리드가 직접 고친다. 열려 있지 않으면 nullptr 입니다. */
        void* getInstance() { return _pCurrent; }
        /** @brief 기본값 인스턴스입니다. 없으면 nullptr 입니다. */
        const void*     getDefaultInstance() const { return _pDefault; }
        const TypeInfo* getType() const { return _pType; }
        const string&   getPath() const { return _path; }

    private:
        /** @brief @p pType 의 기본 생성자로 인스턴스를 만듭니다. 만들 수 없으면 nullptr 입니다. */
        static void* createInstance( const TypeInfo& type );
        static void  destroyInstance( const TypeInfo& type, void* pInstance );
        /** @brief 파일 글 @p fileText 를 @p pInstance 에 엄격하게 읽습니다(JSON 은 파일 전체, 요소 모드는 그 요소). 요소가 없으면 `Missing` 입니다. */
        [[nodiscard]] ConfigReadResult readInto( void* pInstance, string_view fileText ) const;
        [[nodiscard]] string           makeDifferenceJSON() const;
        [[nodiscard]] string           makeDifferenceElement() const;
        /** @brief 요소 모드의 저장 — 파일에서 그 요소의 글만 @p elementText 로 바꿔 씁니다. */
        [[nodiscard]] bool saveElement( const string& elementText ) const;

    private:
        const TypeInfo* _pType;
        void*           _pCurrent;
        void*           _pDefault;
        string          _path;
        string          _elementName; ///< 비면 파일 전체(JSON), 있으면 XML 파일 안의 그 요소

        string           _savedText; ///< 마지막으로 읽거나 저장한 차이 글
        ConfigReadResult _readResult;
    };
} // namespace sw::editor
