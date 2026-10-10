"""
`CheckCodeConventions` 게이트의 규칙 묶음입니다(게이트 파일은 `Scripts/lint/gate/CheckCodeConventions.py` — 진입점 · 파일 고르기 · 보고만 든다).

의존은 아래로만 흐른다(위 모듈이 아래 모듈을 import 한다):

  CrossFile   — 파일 하나로는 알 수 없는 검사(중복 도우미 · 헤더 멤버 초기값 · 비트필드 불리언 · 생성자가 모든 필드를 초기화)
  FileScan    — 파일 하나를 훑어 범위별 규칙을 돌린다 · 파일마다 한 번 읽는 캐시(`scopedTextCache`)
  LineRules   — 줄 단위 규칙(`ConventionRule` 하위 클래스 하나 = 규칙 하나). FileScan 이 import 해 레지스트리에 올린다
  NamingChecks — 명명 판정(컨테이너 · 포인터 · 매개변수 · 지역 변수 · 출력 매개변수)
  Patterns    — 규칙들이 나눠 쓰는 정규식 · 글 도우미
  NamingVocabulary — 컨테이너 접두 표 × 명명 주체 표
  Model       — 위반 · 줄 훑기 문맥 · 규칙 레지스트리

새 줄 규칙은 `LineRules.py` 에 `ConventionRule` 하위 클래스로 더한다(`badSample` 을 `CheckCodeConventionsSelfTest` 가 전수로 본다).
게이트는 파일을 프로세스 덩어리로 훑으니 규칙 객체에 상태를 들지 않는다.
"""
