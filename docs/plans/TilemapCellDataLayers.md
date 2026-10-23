# 타일맵 칸 데이터를 일반 레이어로

> 상태: 구현중 · 선행: 없음 · 영역: gameplay · 옛번호: G-6 · 진행: 2/3

레이어는 0/1(`kArrTileFlagLayerInfo`)뿐이고 워프 · 역할 · 스폰은 전용 원소였습니다. 2026-10-22 사용자 결정으로 착수 조건(두 번째 장르가 칸마다 다른 값을 원함)을 갈음했습니다.

## 1. 정한 것

- 엔진 표의 0/1 플래그 레이어는 그대로 두고, 장르 값은 **맵 데이터가 이름과 기본값을 정하는** 정수 데이터 레이어 `TileMapXMLData::DataLayer` 로 둡니다(Godot TileSet custom data 모양). 장르가 값을 늘려도 엔진 코드를 고치지 않습니다.
- XML: `<dataLayers><layer name=".." default="..">행 우선 정수</layer></dataLayers>`. 레이어가 없는 맵은 이 요소를 쓰지 않아 기존 맵과 바이트가 같습니다.
- 이름 · 기본값 필수, 이름 중복 금지, 값 수 = 셀 수. 어긋나면 맵째 읽기 오류입니다(모자란 칸을 기본값으로 메우지 않음).
- 런타임 `TileMap::findDataLayer` · `getCellData`, 에디터 `TileMapPanel` 의 Data 레이어(칠하기 · 채우기 · 스포이트, 지우기 = 그 레이어 기본값, 레이어 추가).

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| T1 | 형식 · 런타임 조회 · 에디터 페인트 · 테스트 `TileMapXMLTest.DataLayersRoundTripAndRejectBrokenLayers` | 끝 |
| T2 | Debug 빌드(`all EngineTest`, 에디터 모듈 포함, 경고 0) · `TileMapXMLTest.*` 12 개 통과(2026-10-23) | 끝 |
| T3 | 에디터에서 레이어 추가 · 칠하기 · 저장 왕복을 눈으로 확인(에디터 시나리오 · `CaptureEditorUI`) | 확인 대기: 매니페스트 id `tilemap-layers-editor-roundtrip` |
