/**
 * @file UIDocument.h
 * @brief 읽어 둔 UI 문서(`*.ui.xml`)입니다. 화면 서술, 스타일 시트 목록, 위젯 노드 트리를 가지며, 화면은 이 문서로 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/UI/Animation/UIAnimation.h"
#include "Engine/UI/Document/UIBindingDef.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    struct ResourceContext;
    struct TypeInfo;

    class ResourceTagSet;
    class UIDocumentCache;
    class Widget;

    /**
     * @struct UIDocumentNode
     * @brief 문서의 위젯 요소 하나를 파싱한 결과입니다. 인스턴스를 이것으로 만들므로 같은 문서로 화면을 여러 번 열어도 파일은 한 번만 읽습니다.
     * @details `_propertyXML` 은 그 요소에서 자식 위젯과 바인딩 속성을 뺀 XML 이다. 씬 파일과 같은 `XMLSerializer` 가 위젯 PROPERTY 로 읽는다.
     */
    struct UIDocumentNode
    {
        hashed_string        _typeName{};       ///< 위젯 타입 이름(요소 이름)입니다. 만들 때 타입 레지스트리에서 찾으므로, 모듈을 다시 로드해도 이름은 남습니다.
        string               _propertyXML{};    ///< 자식 위젯과 바인딩 속성을 뺀 요소 XML 입니다.
        string               _fragment{};       ///< `UserWidget` 이면 끼울 조각 문서 경로입니다(정규화).
        vector<UIBindingDef> _listBinding{};    ///< 이 요소에서 분리한 바인딩 식입니다(위젯 번호는 무효).
        vector<uint32>       _listChildIndex{}; ///< 자식 노드 인덱스입니다(`UIDocumentAsset::_listNode`, 문서 순서).
        uint32               _sourceLine{ 0 };  ///< 요소가 시작하는 줄입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIDocumentAsset
     * @brief UI 문서 하나입니다(`UIDocumentCache` 가 경로별로 나눠 줍니다). 루트 `UIDocument` 아래에 화면 서술, 스타일 시트 목록, 루트 위젯 하나가 있습니다.
     * @details 형식은 다음과 같다(유니티 UXML, UMG 위젯 블루프린트에 해당).
     *          ```xml
     *          <UIDocument _schemaVersion="0">
     *              <UIScreenDef _layer="Menu" _defaultFocus="Resume" />   (없으면 기본값)
     *              <_listStyleSheet><item>guid:…</item></_listStyleSheet>
     *              <_listAnimation><UIAnimation _name="Open"> … 트랙(_listTrack), 키(_listKey), 이벤트(_listEvent) … </UIAnimation></_listAnimation>
     *              <SafeZonePanel> … 위젯 요소는 리플렉션 타입이고, 속성과 구조체 자식은 PROPERTY 이며, 위젯 자식은 자식 위젯이다 … </SafeZonePanel>
     *          </UIDocument>
     *          ```
     *          모르는 요소, 속성, 열거자, 위젯 타입이 아닌 요소, 패널이 아닌 위젯의 자식은 로드 오류다(파일, 줄). `{` 로 시작하는 속성 값은 바인딩 식이라
     *          값으로 읽지 않고 분리한다(`UIBindingDef`).
     *
     *          읽기(`parse`)는 캐시가 파일마다 한 번, 트리 생성(`instantiate`)은 화면을 열 때마다 한다(유니티 `VisualTreeAsset.Instantiate()` 와 같은 자리).
     *          위젯 요소의 이름은 리플렉션 타입이고, 속성과 구조체 자식 요소는 그 타입의 PROPERTY 이다(씬 파일과 같은 `XMLSerializer` 로 읽는다).
     *          남은 자식 요소 중 위젯 타입은 자식 위젯이고, 나머지는 로드 오류다. 오류 메시지는 `<경로>:<줄>: <이유>` 형식이다(IDE 에서 눌러 그 줄로 갈 수 있다).
     *          위젯은 리플렉션 기본 생성자(`$ctor`)로 만든다. 위젯 타입은 `Widget` 하나만 상속하는 연쇄여야 한다(다중 상속이면 `Widget` 이 첫 기반이어야 한다).
     */
    struct SW_API UIDocumentAsset
    {
        static constexpr uint32 kVersion     = 0;
        static constexpr utf8   kExtension[] = ".ui.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `UIDocument.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "UIDocumentAsset", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
        static constexpr utf8         kRootElementName[] = "UIDocument";

        string                 _path{}; ///< 정규화한 경로입니다(캐시 키).
        UIScreenDef            _screenDesc{};
        vector<string>         _listStyleSheet{}; ///< 스타일 시트 경로입니다(정규화).
        vector<UIAnimation>    _listAnimation{};  ///< 이름 붙은 애니메이션입니다(이름은 겹치지 않으며, `Open` 과 `Close` 는 화면 스택이 재생합니다).
        vector<UIDocumentNode> _listNode{};       ///< 0 번이 루트 위젯입니다.
        vector<string>         _listFragment{};   ///< 이 문서가 조각으로 직접 쓰는 문서입니다(정규화, 중복 없음).

        /**
         * @brief 문서 텍스트 @p text 를 합쳐(`ResourceComposer::compose` — 조건 `_when` · 덮어쓰기 층) 파싱합니다.
         * @param path 문서 경로입니다(오류 메시지와 `UIDocumentAsset::_path` 에 쓰며, 정규화해서 적습니다).
         * @param context 합성 문맥입니다. 캐시는 리소스 id(경로)와 변형의 태그를 싣고(`UITextAssetCache`), 글만 읽는 쪽(테스트)은 `ResourceContext::forDocument()` 입니다.
         *        오류의 줄 번호는 합친 글의 줄입니다(조건이 없는 문서는 원문과 같다).
         * @return 실패하면 그 이유(대상은 `<경로>:<줄>: <까닭>`)입니다. 이때 @p outAsset 은 쓰지 않습니다.
         */
        [[nodiscard]] static expected<void> parse( string_view text, string_view path, const ResourceContext& context, UIDocumentAsset& outAsset );
        /**
         * @brief 이 문서에서 위젯 트리를 만듭니다. 조각(`UserWidget`)은 @p cache 에서 찾아 끼우고 그 안의 이름을 `조각.이름` 으로 감쌉니다.
         * @param tagOverride 조각을 찾을 변형의 태그입니다(문서를 읽은 것과 같은 것 — 빈 집합이면 엔진 판, `UIDocumentCache::acquire`).
         * @param outListBinding 문서에서 분리한 바인딩 식을 위젯 번호와 함께 뒤에 붙입니다(실패하면 비웁니다).
         * @return 루트 위젯입니다. 실패하면(모르는 타입, 읽지 못한 값, 조각이 자기를 다시 부르는 경우) 그 이유(대상은 `<경로>:<줄>: <까닭>`)입니다.
         */
        [[nodiscard]] expected<unique_ptr<Widget>> instantiate( UIDocumentCache& cache, const ResourceTagSet& tagOverride, vector<UIBindingDef>& outListBinding ) const;
        /**
         * @brief 위젯 타입 @p type 의 기본값 인스턴스를 리플렉션 기본 생성자(`$ctor`)로 sw 할당자 블록에 만듭니다. 만들 수 없으면(추상 타입이거나 생성자가 없음) nullptr 입니다.
         * @details `unique_ptr<Widget>` 의 해제자(`sw_delete`)가 가상 소멸자를 부르고 같은 블록을 해제하므로, `make_unique` 로 만든 것과 짝이 맞는다.
         */
        static unique_ptr<Widget> createWidget( const TypeInfo& type );
    };
} // namespace sw
