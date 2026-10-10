"""
CheckCodeConventions — 명명 어휘 — 컨테이너 접두 표(`kMapContainerVocabulary`) × 명명 주체 표(`kMapNamingSubject`). 주체 셋이 함께 쓰는 단 하나의 표다.

게이트 `Scripts/lint/gate/CheckCodeConventions.py` 의 한 묶음이다(묶음 지도는 `__init__.py`).
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field



# --- 3. 명명 어휘 — 주체 셋이 함께 쓰는 단 하나의 표 -------------------------
#
# 멤버 · 매개변수 · 지역변수는 **같은 접두어 표**를 쓴다 (`AGENTS.md` 의 컨테이너/포인터 규칙).
# 주의: 주체마다 표를 따로 적으면 반드시 어긋난다 — 같은 이름이 매개변수면 잡히고 지역변수면 통과하거나
# (`inoutListActors`), 매개변수와 멤버에 정반대 판정이 나온다(`vector<uint8> listBuffer` / `_listBuffer`).
#
# 그래서 **판정은 여기 한 곳에만 있다.** 주체마다 다른 것은 선언을 찾아내는 방법(파싱)뿐이고,
# 찾아낸 이름을 어떻게 볼지는 셋이 이 표를 함께 읽는다. 규칙을 하나 바꾸면 세 주체에 동시에 반영된다.


@dataclass(frozen=True)
class ContainerVocabulary:
    """
    컨테이너 한 종류의 어휘. `AGENTS.md` 의 접두어 표를 그대로 옮긴 것이다.

    - `prefix`       : 요구하는 접두어 (`list` · `map` · `unique` · `arr`)
    - `noun`         : 위반 메시지에 쓰는 우리말 이름
    - `bSingular`    : 단수형을 요구하는가. `unique` 만 예외다 (`AGENTS.md`: `outUniqueIDs` 허용)
    - `bBanListSuffix`: `List` 접미어를 금지하는가 (`actorList` → `listActor`). `list` 만 해당
    - `bByteException`: 바이트 버퍼 이름이면 접두어를 **생략**해야 하는가. `list` 만 해당
    """
    key: str
    prefix: str
    noun: str
    bSingular: bool = True
    bBanListSuffix: bool = False
    bByteException: bool = False


#: 컨테이너 어휘 — 네 종류가 전부다.
kMapContainerVocabulary: dict[str, ContainerVocabulary] = {
    vocabulary.key: vocabulary
    for vocabulary in (
        ContainerVocabulary("list", "list", "동적 배열/리스트", bBanListSuffix=True, bByteException=True),
        ContainerVocabulary("map", "map", "연관 컨테이너"),
        ContainerVocabulary("unique", "unique", "고유 집합", bSingular=False),
        ContainerVocabulary("arr", "arr", "고정 배열"),
    )
}


@dataclass(frozen=True)
class NamingSubject:
    """
    어휘를 적용받는 주체 하나 — 멤버 · 매개변수 · 지역변수.

    - `noun`            : 메시지에 쓰는 말 ("멤버 변수" · "매개변수" · "지역 변수")
    - `bMember`         : 이름이 `_` 로 시작하는가. 멤버만 참이고, 접두어 모양이 여기서 갈린다
    - `listDirection`   : 허용하는 방향 접두어. 멤버에는 없다 (`out`/`inout` 은 매개변수의 것이다)
    - `category`        : 이 주체의 컨테이너/포인터 위반이 달고 나가는 카테고리
    - `mapCategoryByKind`: 멤버만 컨테이너 종류별로 카테고리를 쪼갠다 — 기존 출력을 그대로 둔다
    """
    key: str
    noun: str
    bMember: bool
    category: str
    pointerCategory: str
    listDirection: tuple[str, ...] = ()
    mapCategoryByKind: dict[str, str] = field(default_factory=dict)

    def categoryFor(self, vocabularyKey: str) -> str:
        return self.mapCategoryByKind.get(vocabularyKey, self.category)


#: 주체 — 셋이 전부다. 넷째가 생기면 여기 한 줄이고, 판정은 그대로 물려받는다.
kMapNamingSubject: dict[str, NamingSubject] = {
    subject.key: subject
    for subject in (
        NamingSubject(
            key="member",
            noun="멤버 변수",
            bMember=True,
            category="Naming/DynamicContainer",
            pointerCategory="Naming/RawPointer",
            mapCategoryByKind={
                "list": "Naming/DynamicContainer",
                "map": "Naming/MapContainer",
                "unique": "Naming/SetContainer",
                "arr": "Naming/FixedArray",
            },
        ),
        NamingSubject(
            key="parameter",
            noun="매개변수",
            bMember=False,
            category="Naming/ParameterContainer",
            pointerCategory="Naming/ParameterPointer",
            listDirection=("out", "inout"),
        ),
        NamingSubject(
            key="local",
            noun="지역 변수",
            bMember=False,
            category="Naming/LocalContainer",
            pointerCategory="Naming/LocalPointer",
            listDirection=("out", "inout"),
        ),
    )
}

#: 컨테이너 타입을 알아보는 정규식 — 주체 셋(매개변수 · 지역 · 멤버)이 같은 것을 본다. 따로 적으면
#: `deque` 가 한 곳에만 있거나 `sw::` 접두어가 빠지는 식으로 갈린다.
_kAnyVectorRe = re.compile(r'\b(?:(?:sw::)?(?:vector|list|deque))\s*<([^>]+)>')
_kAnyMapRe = re.compile(r'\b(?:(?:sw::)?(?:unordered_map|map))\s*<')
_kAnySetRe = re.compile(r'\b(?:(?:sw::)?(?:unordered_set|set))\s*<')

#: 포인터 선언처럼 보이지만 선언이 아닌 키워드 — `delete pObject;` 를 변수 선언으로 읽지 않기 위한 것.
_kNonDeclarationKeyword = (
    "delete", "return", "sizeof", "static_cast", "reinterpret_cast",
    "dynamic_cast", "const_cast", "case", "default",
)

#: 지역 고정 배열 선언. 기본 타입만 본다 — 사용자 타입까지 열면 함수 호출과 구별이 안 된다.
_kLocalFixedArrayRe = re.compile(
    r'^\s*(?:float32|float64|int32|int64|uint8|uint16|uint32|uint64|char|utf8|bool)'
    r'\s+([a-zA-Z0-9_]+)\s*\[[^\]]+\]\s*(?:=|;|{)'
)

#: 바이트 벡터로 치는 원소 타입. `vector<uint8>` 류는 `list` 접두어를 붙이지 않는다.
_kByteInnerTypeRe = re.compile(r'\b(?:uint8|int8|utf8|char|byte)\b', re.IGNORECASE)

#: 이름에 이 단어가 들어 있으면 바이트 버퍼로 본다 (`_bytes` · `outBytes` · `rawBuffer`).
_kListByteWord = ("byte", "bytes", "buffer")


def listAllowedPrefixInternal(subject: NamingSubject, prefix: str) -> tuple[str, ...]:
    """
    주체 하나가 어떤 접두어들을 허용받는지. **접두어 모양은 여기서만 만든다.**

    멤버는 `_list` · `_s_list`, 나머지는 `list` · `outList` · `inoutList` 가 된다.
    """
    if subject.bMember:
        return (f"_{prefix}", f"_s_{prefix}")

    capitalized = prefix[0].upper() + prefix[1:]
    return (prefix,) + tuple(f"{direction}{capitalized}" for direction in subject.listDirection)


def isByteBufferNameInternal(innerType: str, varName: str) -> bool:
    """원소 타입이 바이트이고 이름이 그렇게 말하고 있는가 — 세 주체가 같은 답을 받는다."""
    if not _kByteInnerTypeRe.search(innerType):
        return False

    lowered = varName.lstrip("_").lower()
    return any(word in lowered for word in _kListByteWord)
