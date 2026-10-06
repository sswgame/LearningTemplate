# MeadowVillage — 키트 조립 테스트 게임

## 이 게임으로 무엇을 배우나

농장 키트(`GF_Farming`)와 생물 마을 키트(`GF_CreatureLife`)를 한 씬에 섞은 게임입니다. 키트 둘이 같은 돈과 같은 시계를 볼 때 어떻게 나누는지를 보여 줍니다.

- 공유 상태(`GameStateComponent`) 하나에 돈, 시계, 퀘스트 일지, 호감도를 두는 방법
- 공유 상태와 키트 디렉터 둘을 한 오브젝트에 붙이고, 붙인 순서를 틱 순서로 쓰는 방법
- 두 디렉터가 입력 맵 하나를 액션 이름 접두로 나눠 쓰는 방법

규칙은 [Kits](../../GameFramework/Kits/README.md)의 "키트 여럿을 한 게임에"에 있습니다.

## 빌드와 실행

```powershell
cmake --preset Ninja-Debug-MeadowVillage
cmake --build --preset Ninja-Debug-MeadowVillage
cd build/Ninja-Debug-MeadowVillage/Bin
./App.exe -dx12
./App.exe -dx12 -gv_meadowAutoPlay=1    # 늘 8배속
```

이 게임에는 아직 자동화 시나리오가 없습니다. 같은 흐름을 EngineTest 의 `KitCompositionTest` 가 모의 디렉터로 확인하고,
SmokeTest 의 `ArchitectureTest.LiveReloadOneOfTwoKitsCascadesIntoTheGameOnly` 가 키트 하나만 다시 로드해도 게임만 함께 다시 로드되는지 봅니다.

## 조작

| 키 | 액션 | 하는 일 |
|----|------|---------|
| Space(누르는 동안) | `Village.FastForward` | 8배속 |
| E | `Town.Talk` | 생물과 대화(하루 한 번) |

키 배치는 `data/meadow.input.xml` 에 있습니다. 액션 이름이 게임 접두(`Village.`, `Town.`)로 갈려 있어서, 같은 키를 두 디렉터가 읽으면 이름이 달라 드러납니다.

## 구조

씬은 `Resource/game/meadowvillage/maps/meadow.scene.xml` 하나입니다. 씬의 `VillageState` 오브젝트에 **공유 상태, 밭 디렉터, 마을 디렉터** 순서로 컴포넌트가 붙어 있고, 그 순서가 틱 순서입니다.
돈, 시계, 퀘스트 일지, 호감도(공유 평판의 세력 `creature.<종>`)는 공유 상태 하나에 있습니다.

- **밭**(`MeadowFarmDirectorComponent`)은 4 × 2 크기의 순무 밭입니다. 하루(실제 2분)가 지나면 자라고, 다 자란 작물은 거둬 공유 지갑에 30G 에 팝니다.
  시작 돈 20G 는 새 게임일 때만 넣습니다.
- **마을**(`MeadowTownDirectorComponent`)에는 풀밭 서식지(`meadow`) 둘이 있습니다. 낮에 `sprout` 가 찾아와 과수원 서식지(`orchard`)를 하나 부탁합니다.
  공유 지갑에 50G 가 모이면 같은 틱에 나무 두 그루를 심어 부탁이 끝납니다. 밭이 번 돈으로 마을이 부탁을 들어주는 흐름입니다.
- **카메라**는 씬의 고정 직교 카메라입니다. 카메라를 미는 것은 한 디렉터만 한다는 조립 규칙 때문에, 두 디렉터 모두 카메라를 만지지 않습니다.

## 더 볼 곳

- [Kits](../../GameFramework/Kits/README.md) — 키트 조립 규칙
- [HarvestValley](../HarvestValley/README.md) — 농장 키트 하나만 쓰는 게임
