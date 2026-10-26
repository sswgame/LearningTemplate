# JSON 읽기와 쓰기

> 질문: JSON(과 같은 모양의 XML)을 읽거나 쓸 때 무엇을 쓰는가?
> 독자: JSON 설정 · 페이로드 · 보고 · 트레이스나 XML 출력을 읽거나 만드는 사람.
> 결론: 읽기와 트리 편집은 `JSONDocument` · `XMLDocument`, 처음부터 만들어 내보내는 출력은 `JSONWriter` · `XMLWriter` 다. 글을 `+` 로 이어 붙이지 않는다.

| 할 일 | 쓰는 것 |
|---|---|
| 파일 · 응답을 읽고 값을 꺼낸다, 트리를 고쳐 다시 쓴다 | `JSONDocument` · `JSONValue`, `XMLDocument` · `XMLNode` |
| 리플렉션 객체를 읽고 쓴다 | `JSONSerializer`(`../Format`) |
| 페이로드 · 보고 · 트레이스를 앞에서부터 쓴다 | `JSONWriter` |
| 서명 · 전송할 JSON 본문을 넘긴다 | `JSONWriter()` → `finish()` 가 주는 `JSONText` |
| 줄 배치를 지켜 XML 을 앞에서부터 쓴다(녹화한 시나리오, 만든 그래프) | `XMLWriter`(`../XML/XMLWriter.h`) |

`JSONWriter` 의 규칙(헤더 주석이 정본이다):

- 키와 문자열 값은 늘 이스케이프된다(`"` · `\` · 제어 문자, 잘못된 UTF-8 은 U+FFFD 이스케이프). 쉼표 · 콜론 · 괄호는 쓰기기가 놓는다. 그래서 부르는 쪽에 `escapeString` 이나 따옴표가 나오면 잘못 쓴 것이다.
- 순서 위반(배열 안의 키, 키 없는 객체 값, 짝이 안 맞는 닫기)은 단언이고, NaN · 무한대는 `null` 을 쓰되 실패로 남는다. `isComplete()` 로 확인한다.
- 대상은 쓰기기가 가진 버퍼(`finish` 로 `JSONText` 를 꺼낸다), 문자열 버퍼(큰 출력은 `reserve` 해 둔다), 고정 버퍼다. 고정 버퍼는 할당하지 않아 멈춘 스레드 옆에서 쓰는 행 감시 보고가 쓴다.
- `JSONText` 는 쓰기기만 만든다. JSON 본문을 받는 API(`JSONWebToken::makeSigned`)가 이것을 받으면 이어 붙인 글이 들어올 자리가 없다.
- indent 가 음수이면 한 줄, 0 이상이면 `JSONDocument::dump( indent )` 와 같은 모양이다.
- 시각처럼 소수 자리가 정해진 수는 `valueFixedPoint` 로 쓴다(실수를 거치지 않는다). 이미 파싱한 값을 옮길 때는 `valueJSON` 이다.

`XMLWriter` 는 같은 모양이다. 속성 값 · 글은 늘 이스케이프되고 원소 · 속성 이름은 XML 이름 글자만 받는다. 줄 배치는 `whitespace` 로만 넣고, 나중에 채우는 자리는 따로 쓴 쓰기기를 `fragment` 로 옮긴다.
원문의 주석 · 들여쓰기를 지키며 한 구간만 고치는 편집기(`CatalogXMLText`)는 값 하나를 `XMLDocument::escapeString` 으로 바꿔 끼운다 — 이것이 그 자리의 단위다.

도입 이유는 [결정 기록 5-4](../../../../docs/decisions/5-4_DataConfig.md) "JSON 을 글로 이어 붙여 만들지 않고" 항목에 있다.
