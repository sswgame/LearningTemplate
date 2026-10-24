# KayKit 캐릭터 텍스처를 머티리얼로

> 상태: 측정대기 · 선행: 없음 · 영역: gameplay · 옛번호: D4-6 · 진행: 2/2

Shooter3D 팩의 아틀라스(`textures_raw/kaykit_*.png` → `textures/kaykit_*.dds`)와 머티리얼(`materials/kaykit_*.material`)을 다른 게임도 쓰게 했다.
KayKit 캐릭터를 그리는 다른 곳은 Empty 의 벤치뿐이었다(`data/benchscene.xml` — 기사 · 리그 무기 · 망토 · 전투 적 · 칼이 흰 기본 머티리얼이었다).
벤치 설정에 머티리얼 칸(`_knightMaterial` · `_rigWeaponMaterial` · `_rigCapeMaterial` · `_combatEnemyMaterial` · `_combatSwordMaterial`)을 두고 유닛이 `setMaterialPath` 로 받는다.
StarSkirmish 의 KayKit Space 아틀라스(`textures/kaykit_space.dds`)는 꺼내 두었으나 그 메시(`models/kaykit_space/*`)를 쓰는 씬 · 프리팹이 없어 머티리얼을 만들지 않았다(쓰는 날 같은 모양으로 만든다).

## K2 화면 확인

벤치의 캐릭터 · 리그 · 전투 장면을 캡처하는 시나리오를 만들었다(`game/empty/automation/kaykitcharacters.scenario.xml` · `kaykitrig.scenario.xml` · `kaykitcombat.scenario.xml`, 변수 `-gv_benchCharacters` · `-gv_benchRig` · `-gv_benchCombat`).
사진을 보고 아틀라스 색이 입혀졌는지 눈으로 확인한다. 텍스처 샘플이 늘어 벤치 수치가 바뀔 수 있어, 같은 인자로 `GT.Animation` 측정 기준을 다시 잡는다.

**남은 확인(통합에서 확인):** 매니페스트 id `kaykit-bench-scenarios`(사진), 매니페스트 id `kaykit-bench-rebaseline`(측정 기준 다시 잡기). 끝나면 통합 담당이 이 문서를 닫는다.
