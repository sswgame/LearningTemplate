/**
 * @file UIDocument.h
 * @brief 읽어 둔 UI 문서(`*.ui.xml`) — 화면 서술 · 스타일 시트 목록 · 위젯 노드 트리입니다. 화면은 이것에서 지어집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Animation/UIAnimation.h"
#include "Engine/UI/Document/UIBindingDesc.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /**
     * @struct UIDocumentNode
     * @brief 문서의 위젯 원소 하나를 파싱한 결과입니다. 인스턴스는 이것에서 지어지므로 같은 문서로 화면을 여러 번 열어도 파일은 한 번만 읽습니다.
     * @details `_propertyXML` 은 그 원소에서 자식 위젯과 바인딩 속성을 뺀 XML 입니다 — 씬 파일과 같은 `XMLSerializer` 가 위젯 PROPERTY 로 읽습니다.
     */
    struct UIDocumentNode
    {
        hashed_string         _typeName{};       ///< 위젯 타입 이름(원소 이름) — 지을 때 타입 등록부에서 찾는다(모듈 다시 로드에도 이름은 남는다)
        string                _propertyXML{};    ///< 자식 위젯 · 바인딩 속성을 뺀 원소 XML
        string                _fragment{};       ///< `UserWidget` 이면 끼울 조각 문서 경로(정규화)
        vector<UIBindingDesc> _listBinding{};    ///< 이 원소에서 뗀 바인딩 식(위젯 번호는 무효)
        vector<uint32>        _listChildIndex{}; ///< 자식 노드 자리(`UIDocumentAsset::_listNode`) — 문서 순서
        uint32                _sourceLine{ 0 };  ///< 원소가 시작하는 줄
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIDocumentAsset
     * @brief UI 문서 하나입니다(`UIDocumentCache` 가 경로로 나눠 준다). 루트 `UIDocument` 아래 화면 서술 · 스타일 시트 목록 · 루트 위젯 하나.
     * @details 형식(유니티 UXML · UMG 위젯 블루프린트의 자리):
     *          ```xml
     *          <UIDocument _schemaVersion="1">
     *              <UIScreenDesc _layer="Menu" _defaultFocus="Resume" />   (없으면 기본값)
     *              <_listStyleSheet><item>engine/ui/styles/default.uistyle.xml</item></_listStyleSheet>
     *              <_listAnimation><UIAnimation _name="Open"> … 트랙(_listTrack) · 키(_listKey) · 사건(_listEvent) … </UIAnimation></_listAnimation>
     *              <SafeZonePanel> … 위젯 원소 = 리플렉션 타입, 속성 · 구조체 자식 = PROPERTY, 위젯 자식 = 자식 위젯 … </SafeZonePanel>
     *          </UIDocument>
     *          ```
     *          모르는 원소 · 속성 · 열거자, 위젯 타입이 아닌 원소, 패널이 아닌 위젯의 자식은 로드 오류입니다(파일 · 줄). `{` 로 시작하는 속성 값은 바인딩 식이라
     *          값으로 읽지 않고 뗍니다(`UIBindingDesc`).
     */
    struct UIDocumentAsset
    {
        static constexpr uint32 kVersion           = 1; ///< `_schemaVersion` — 옛 형식 리더는 두지 않는다
        static constexpr utf8   kExtension[]       = ".ui.xml";
        static constexpr utf8   kRootElementName[] = "UIDocument";

        string                 _path{}; ///< 정규화한 경로(캐시 열쇠)
        UIScreenDesc           _screenDesc{};
        vector<string>         _listStyleSheet{}; ///< 스타일 시트 경로(정규화)
        vector<UIAnimation>    _listAnimation{};  ///< 이름 붙은 애니메이션(이름이 겹치지 않는다 — `Open` · `Close` 는 화면 스택이 재생)
        vector<UIDocumentNode> _listNode{};       ///< 0 이 루트 위젯
        vector<string>         _listFragment{};   ///< 이 문서가 조각으로 바로 쓰는 문서(정규화, 중복 없음)
    };
} // namespace sw
