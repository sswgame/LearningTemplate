/**
 * @file UIDocument.h
 * @brief 읽어 둔 UI 문서(`*.ui.xml`)입니다. 화면 서술, 스타일 시트 목록, 위젯 노드 트리를 가지며, 화면은 이 문서로 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/UI/Animation/UIAnimation.h"
#include "Engine/UI/Document/UIBindingDesc.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /**
     * @struct UIDocumentNode
     * @brief 문서의 위젯 요소 하나를 파싱한 결과입니다. 인스턴스를 이것으로 만들므로 같은 문서로 화면을 여러 번 열어도 파일은 한 번만 읽습니다.
     * @details `_propertyXML` 은 그 요소에서 자식 위젯과 바인딩 속성을 뺀 XML 이다. 씬 파일과 같은 `XMLSerializer` 가 위젯 PROPERTY 로 읽는다.
     */
    struct UIDocumentNode
    {
        hashed_string         _typeName{};       ///< 위젯 타입 이름(요소 이름)입니다. 만들 때 타입 레지스트리에서 찾으므로, 모듈을 다시 로드해도 이름은 남습니다.
        string                _propertyXML{};    ///< 자식 위젯과 바인딩 속성을 뺀 요소 XML 입니다.
        string                _fragment{};       ///< `UserWidget` 이면 끼울 조각 문서 경로입니다(정규화).
        vector<UIBindingDesc> _listBinding{};    ///< 이 요소에서 분리한 바인딩 식입니다(위젯 번호는 무효).
        vector<uint32>        _listChildIndex{}; ///< 자식 노드 인덱스입니다(`UIDocumentAsset::_listNode`, 문서 순서).
        uint32                _sourceLine{ 0 };  ///< 요소가 시작하는 줄입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIDocumentAsset
     * @brief UI 문서 하나입니다(`UIDocumentCache` 가 경로별로 나눠 줍니다). 루트 `UIDocument` 아래에 화면 서술, 스타일 시트 목록, 루트 위젯 하나가 있습니다.
     * @details 형식은 다음과 같다(유니티 UXML, UMG 위젯 블루프린트에 해당).
     *          ```xml
     *          <UIDocument _schemaVersion="1">
     *              <UIScreenDesc _layer="Menu" _defaultFocus="Resume" />   (없으면 기본값)
     *              <_listStyleSheet><item>engine/ui/styles/default.uistyle.xml</item></_listStyleSheet>
     *              <_listAnimation><UIAnimation _name="Open"> … 트랙(_listTrack), 키(_listKey), 이벤트(_listEvent) … </UIAnimation></_listAnimation>
     *              <SafeZonePanel> … 위젯 요소는 리플렉션 타입이고, 속성과 구조체 자식은 PROPERTY 이며, 위젯 자식은 자식 위젯이다 … </SafeZonePanel>
     *          </UIDocument>
     *          ```
     *          모르는 요소, 속성, 열거자, 위젯 타입이 아닌 요소, 패널이 아닌 위젯의 자식은 로드 오류다(파일, 줄). `{` 로 시작하는 속성 값은 바인딩 식이라
     *          값으로 읽지 않고 분리한다(`UIBindingDesc`).
     */
    struct UIDocumentAsset
    {
        static constexpr uint32 kVersion     = 1; ///< `_schemaVersion` 입니다. 이전 형식 리더는 두지 않습니다.
        static constexpr utf8   kExtension[] = ".ui.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `UIDocumentLoader.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "UIDocumentAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        static constexpr utf8         kRootElementName[] = "UIDocument";

        string                 _path{}; ///< 정규화한 경로입니다(캐시 키).
        UIScreenDesc           _screenDesc{};
        vector<string>         _listStyleSheet{}; ///< 스타일 시트 경로입니다(정규화).
        vector<UIAnimation>    _listAnimation{};  ///< 이름 붙은 애니메이션입니다(이름은 겹치지 않으며, `Open` 과 `Close` 는 화면 스택이 재생합니다).
        vector<UIDocumentNode> _listNode{};       ///< 0 번이 루트 위젯입니다.
        vector<string>         _listFragment{};   ///< 이 문서가 조각으로 직접 쓰는 문서입니다(정규화, 중복 없음).
    };
} // namespace sw
