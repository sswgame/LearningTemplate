# Appearance — 캐릭터 외형 데이터와 해석

입힌 장비 목록 → 실제로 그릴 외형. 모두 사람이 고치는 데이터이고, 코드는 데이터를 읽고 정해진 순서로 한 번 푸는 해석기뿐이다.
결과(`ResolvedAppearance`)를 형상 쪽(`Engine/Character` — 소켓 · 체형 · 피팅 · 병합)과 외형 컴포넌트(`CharacterAppearanceComponent` — 아래 "오브젝트로 조립")가 쓴다.
2D(스프라이트 종이 인형)와 3D(메시)가 같은 길을 탄다 — 부품 종류만 다르다.

## 파일과 형식 (`<게임>/data/appearance/`, 예: `Resource/game/shooter3d/data/appearance/`)

모르는 속성 · 원소 · 이름은 모두 **로드 오류**다(`AppearanceLoadReport`, 경고 로그도 남아 `ResourceDataSchemaTest` 가 잡는다).
`AppearanceDatabase::loadFromFolder( 폴더, &아이템카탈로그 )` 가 아래 파일을 읽고 `finishLoad` 가 서로의 이름을 대조한다.
**`None` 은 이름으로 쓸 수 없다** — `hashed_string( "None" )` 은 빈 이름이다(언리얼 `FName` 과 같다). 항목은 `Off` · `Bare` 처럼 짓는다.

| 파일 | 루트 | 무엇 |
|---|---|---|
| `slots.xml` | `SlotTable` | 칸 이름 · 받는 종류(순서 = 해석 순서), 칸 묶음(`Occupancy` — 로브는 상의 + 하의, 양손 무기는 두 손) |
| `sets.xml` | `EquipSetCatalog` | 세트 — 칸별 조각, 몸 종류 변형, 세트 완성 표현 |
| `itemvisuals.xml` | `ItemVisualCatalog` | 아이템 외형 — 부품 · 상태별 소켓 배치 · 피해 단계 |
| `customization.xml` | `CustomizationSchemaCatalog` | 꾸미기 스키마(캐릭터 · 아이템 공용) |
| `rules.xml` | `AppearanceRuleTable` | 외형 규칙(조건 → 동작, 우선순위) |
| `presets.xml` | `CharacterAppearanceCatalog` | 캐릭터 외형 프리셋(`CharacterAppearance`) |

아이템 자체는 `<게임>/data/items.xml`(`ItemCatalog`) — `visual` 이 외형 id, `<Requires>` 가 장착 조건, `breakPolicy` 가 조건이 깨질 때의 처리다.

```xml
<SlotTable><Slot name="Head"/><Slot name="MainHand" accept="Weapon"/><Slot name="OffHand" accept="Weapon"/>
  <Occupancy id="TwoHanded" slots="MainHand,OffHand"/></SlotTable>

<EquipSetCatalog><Set id="Recon"><Piece slot="Head" items="helmet_recon"/><Piece slot="Body" items="vest_recon"/>
  <Variant bodyType="Female"><Piece slot="Body" items="vest_recon_f"/></Variant>
  <Complete visual="recon_ghillie" slots="Body,Feet"/></Set></EquipSetCatalog>

<ItemVisual id="rifle_m4" occupancy="TwoHanded" customization="Rifle" tags="Weapon.Rifle" defaultState="Drawn">
  <Part name="Body" kind="SocketPrefab" prefab="…/rifle_m4.prefab.xml" sockets="…/rifle_m4.sockets.xml" socket="handslot.r">
    <Variant name="Cracked" prefab="…"/><MaterialVariant name="Desert" material="…"/></Part>
  <Part name="Press" kind="BodyModification"><Morph name="HeadSquash" weight="0.3"/><Material name="Dirt" value="1"/><HideRegion name="Scalp"/></Part>
  <Part name="Visor" kind="SocketPrefab" prefab="…" socket="head" breakable="true" breakStage="Shattered" impulse="0 1 2"/>
  <State name="Slung"><Place part="Body" socket="RifleSling,chest" offset="0 0 -0.25" rotation="0 0 35"/><HidePart part="Visor"/></State>
  <DamageStage name="Worn" threshold="0.5"><Variant part="Body" name="Cracked"/><Material part="Body" name="Wear" value="0.5"/></DamageStage>
</ItemVisual>

<Schema id="Rifle">
  <Slider name="Height" category="Body" symmetry="…" min="0" max="1" default="0.5"><Drive kind="BoneProportion" target="Height" from="0.92" to="1.08"/></Slider>
  <Color name="GripWrap" category="Dye" default="0.1 0.1 0.1 1"><Drive kind="DyeChannel" target="Grip" channel="0"/></Color>
  <Choice name="Finish" default="Standard"><Option name="Standard"/><Option name="Desert" materialVariant="Desert"/></Choice>
  <Attachment name="Optic" socket="Rail" default="IronSights"><Option name="IronSights"/><Option name="Scope4x" prefab="…" offset="0 0.01 0"/></Attachment>
  <Slider name="BeardLength"><Condition parameter="Beard" options="Stubble"/><Drive kind="Morph" target="BeardLength"/></Slider>
</Schema>

<Rule id="FullFaceHelmetHidesHair" priority="10"><When target="Head" tag="Helmet.FullFace"/><Hide target="Hair"/><Hide region="Scalp"/></Rule>
<Rule id="BackpackMovesRifleSling"><When occupied="Back"/><OverrideSocket name="RifleSling" parent="Back.Strap,chest" offset="0.1 0 -0.3"/></Rule>

<CharacterAppearance id="Soldier" parent="HumanBase" bodyShape="Average,Heavy" tags="Faction.Army">
  <Value name="Height" min="0.35" max="0.75"/><Value name="Hair" options="Short,Buzz"/><Value name="SkinTone" colors="0.86 0.67 0.53 1; 0.40 0.28 0.20 1"/>
  <Equip set="Recon"/><Equip slot="Head" item=""/>
  <Equip slot="MainHand" item="rifle_m4" state="Slung" visible="rifle_gold"><Value name="Optic" option="Scope4x"/></Equip>
  <SocketOverride name="RifleSling" parent="spine2" offset="0 0 -0.2"/></CharacterAppearance>
```

- 부품 종류: `Skinned`(캐릭터 스켈레톤을 따르는 메시 — `deforms="false"` 면 본 하나를 따르는 단단한 조각, `skeleton` 이 비면 뿌리 본 하나의
  암묵 스켈레톤), `SocketPrefab`(소켓 부착 프리팹 — 몇 개든, 서로 다른 소켓에), `BodyModification`(몸에 거는 모프 · 머티리얼 값 · 숨김 영역),
  `Sprite`(2D — `layer` 가 그리기 순서). 부품마다 자기 소켓 에셋(`sockets`)을 가진다.
- 소켓 이름은 해석된 외형의 **한 이름 공간**이다 — 몸 소켓(본 이름 포함)은 그대로, 부품 소켓은 주인 이름이 앞에 붙는다(`MainHand.Muzzle`).
  첫 마디가 칸 · 꾸미기 매개변수 이름이면 부품 소켓이다. `socket="Belt.Hook,hip.l"` 은 후보 목록 — 앞에서부터 있는 것(부착 사슬의 대체).
- 프리셋 상속: 값은 매개변수별, 칸은 칸별, 소켓 덮어쓰기는 이름별로 아래 층이 덮는다. 장비는 **층마다 세트 → 칸** 순서(아래 층 세트가 위 층 칸을 덮는다).
  쉼표 목록 · 범위 · 색 목록은 씨앗으로 뽑는다 — 키(매개변수 · 칸 이름)마다 따로 섞어, 매개변수를 하나 더해도 다른 뽑기가 바뀌지 않는다.
- 규칙 조건(`When`): `target`+`tag`(그 칸의 **보이는** 외형 태그 — 형상 변경이면 보이는 쪽), `tag`(아무 외형), `characterTag`, `occupied`,
  `bodyShape`, `bodyType`, `not="true"`. 동작: `Hide target|itemTag|region`, `Variant`, `SwapMesh`, `SwapMaterial`, `Morph`, `OverrideSocket`.
  같은 대상을 다르게 바꾸는 두 규칙은 우선순위가 이긴다. **같은 우선순위면 로드 오류** — 한쪽이 같은 질문을 `not` 으로 뒤집은 짝만 허락한다.
- 꾸미기 조건은 **앞에 선언한** 매개변수만 가리킨다(순환이 생길 수 없다). 대칭 묶음은 `CustomizationUtil::applyValue( …, bSymmetric )` 가 함께 바꾼다.
- 장착 조건(`ItemDef` 의 `<Requires set="S"/>` · `set="S" pieces="2"` · `equippedTag` · `characterTag` · `bodyShape`)은 게임플레이라
  `Equipment::evaluateEquip` · `canEquip` 이 판정한다. 깨질 때의 정책(`breakPolicy`)은 `UnequipTogether`(기본 — 함께 벗겨 돌려줌) · `KeepHidden`
  (낀 채 숨김, 능력치 · 다른 조건에서 빠짐) · `RefuseUnequip`(깨뜨리는 벗기 · 바꿔 끼기를 거부). 조건끼리의 순환은 로드 오류(`findConditionCycle`).
  세트 정보는 `Equipment::setSetLookup( &database.getSets() )` 로 빌려 준다. 세트 효과(수치)는 어빌리티 데이터 몫이다.

## 해석 (`AppearanceResolver::resolve` — 순수 함수)

입력은 펼친 프리셋(`CharacterAppearanceSpec` — `CharacterAppearanceCatalog::expand( id, seed, … )`)이고, 장비는 `AppearanceInputUtil::applyEquipment`
가 칸에 덮는다(아이템 · 인스턴스 상태 · 숨김, 피해 = 맞은 피해와 닳은 내구도 중 큰 쪽). 정해진 순서로 한 번 돈다:

1. 몸 외형 + 캐릭터 꾸미기(값을 스키마 범위 · 격자로 맞춤, 꺼진 매개변수는 건너뜀, 고르기 외형은 매개변수 이름이 주인)
2. 칸 — 보이는 외형(형상 변경 `_visibleVisual`), 장착 조건이 깨져 숨긴 장비
3. 칸 점유 — 표 순서로, 먼저 보이는 장비의 점유가 이긴다
4. 세트 완성 표현 — 요청된 아이템(숨김 정책으로 꺼진 것은 빼고)으로 판정, 몸 종류 변형 조각
5. 규칙 — **3 · 4 뒤, 규칙 동작 전의 목록만** 보고 판정(규칙끼리 사슬이 없다). 진 쪽도 설명(`_listTrace`)에 남는다
6. 출력 — 장비 꾸미기(부착물 소켓은 `<칸>.<소켓>`), 피해 단계(문턱 이상인 마지막 단계의 변형 · 머티리얼 값), 떨어진 부품, 외형 상태의 배치,
   변형 우선순위 규칙 > 피해 단계 > 꾸미기, 메시 · 머티리얼 바꾸기, 모프, 소켓 덮어쓰기

결과의 `_hash` 는 캐시 키(설명은 빼고 모든 출력), `_meshHash` 는 병합 메시에 드는 것만(스킨드 부품 · 모프 · 머티리얼 값 · 본 비율 · 숨김 영역) —
외형 상태(뽑음 ↔ 멤)나 부착물만 바뀌면 `_meshHash` 는 그대로다. 해시는 이름의 내용 해시로 쌓아 프로세스 · 기계가 달라도 같다.

**값은 전송 정밀도로 저장한다**: 슬라이더는 범위의 65535 칸, 색은 성분마다 255 칸, 피해는 255 칸. 해석기는 맞춘 값만 쓰므로 네트워크 · 공유 코드로
오간 값이 비트까지 같고 해시가 같다.

`CharacterAppearanceState` 가 외형 컴포넌트의 상태다 — `update( &equipment )` 는 장비 `getRevision` · 데이터 `getRevision`(다시 읽으면 오른다) ·
입력(프리셋 교체 · 형상 변경 · 외형 상태 · 꾸미기)이 바뀔 때만 다시 해석한다. 떨어져 나감 이벤트(`takeDetachEvents`)는 지난 해석에 붙어 있던 부품이
떨어질 때 한 번만 나온다(부품 · 아이템 · 떨어지기 전 자리 · 충격 힌트) — 이미 부서진 채 스폰된 NPC 는 이벤트가 없다.

## 선택 · 공유 코드 · 플레이어 프리셋 · 네트워크

- `AppearanceSelection` = 기준 프리셋 + 씨앗 + 몸 종류 · 체형 · 얼굴 + 꾸미기 값 + 칸(아이템 · 형상 변경 · 상태 · 피해 · 떨어진 부품 · 아이템 꾸미기).
  `_listCategory` 가 있으면 부분 프리셋(스키마 `category` — 머리 · 얼굴 · 염색, 장비 구성은 `Loadout`).
- `AppearanceSelectionUtil::applySelection` 은 지금 콘텐츠에 맞춰 펼친다 — 지워진 매개변수 · 항목은 버리고 보고, 새 매개변수는 기본값, 지워졌거나
  잠긴(`IAppearanceUnlockQuery`) 아이템은 칸 기본(기준 프리셋의 것)으로 되돌리고 보고(`AppearanceSelectionReport`). 실패하지 않는다.
  `previewSelection` 은 입혀 보기(해석만, 바꾸지 않음).
- `AppearanceSelectionCodec` — 비트 형식(`BitWriter`). 이름은 32 비트 내용 해시(로드가 해시 충돌을 막는다), 값마다 종류 2 비트라 모르는 매개변수도
  건너뛴다. 받는 쪽이 모르는 해시는 `#xxxxxxxx` 자리 이름이 되어 펼칠 때 "지워진 콘텐츠" 로 보고된다. 네트워크 외형 동기화가 이것을 그대로 보낸다
  — 받는 쪽은 `readSelection` → `applySelection` → `resolve` 로 보낸 쪽과 같은 해시를 얻는다(캐릭터 태그는 게임플레이가 따로 복제한다).
- `AppearanceShareCode` — 판 1 바이트 + 선택 + CRC32 를 base64url 로. 체크섬 · 판 · 잘림을 거절한다.
- `UserAppearancePresetStore`(`SaveGame`) — 이름 칸 · 즐겨찾기 · 썸네일 경로 · 내용(공유 코드 한 줄)을 `key=value` 로. **플레이어 세이브는 배포된
  데이터라 옛 판을 읽는다**(엔진 데이터의 "옛 형식 리더 없음" 규칙과 다르다) — `formatVersion` 마다 다음 판으로 올리는 단계가 있다(판 1 → 2).

## 오브젝트로 조립 — `CharacterAppearanceComponent` · `AppearanceSocketRig`

몸 유닛(같은 오브젝트의 `SkeletalMeshComponent`)에 붙이고 프리셋 id · 씨앗을 준다. 외형 데이터는 게임 서비스 `AppearanceDatabase`(게임이 `loadFromFolder` 로 읽어 건다)다.

- **조립**(틱 뒤 게임 스레드): 프리셋을 펼치고 칸 덮어쓰기(`setSlotItem` — 무기 바꾸기)를 얹어 해석한다. 몸 부품(주인이 빈 첫 `Skinned`)은 이 오브젝트의
  유닛에 메시 · 스켈레톤 · 머티리얼로, 다른 `Skinned` 부품은 몸을 리더로 따르는 자식 유닛으로, `SocketPrefab` 부품은 프리팹을 세워 `SocketBindingComponent` 로
  소켓에 붙인다. 다시 조립할 때 같은 부품(주인 · 이름 · 에셋)은 오브젝트를 그대로 둔다(무기를 바꿔도 투구는 그대로).
- **소켓 이름 공간**(`AppearanceSocketRig`, 값 타입 — 씬 없이 시험): 유닛 0 = 몸(몸 부품의 소켓 에셋 + 외형의 소켓 덮어쓰기), 그 뒤 소켓 에셋을 가진 부품마다
  강체 유닛(본 하나 `root`)이 칸 이름을 앞에 달고 들어온다(`MainHand.Muzzle`). 부품 자리 = 배치 오프셋 × 소켓 × 지금 본. 같은 이름은 다시 지어도 같은 번호다.
- **포즈를 따라감**: 몸 유닛에 단계 일(`CharacterAppearanceSocketTask`)을 걸어 몸의 포즈가 끝난 프레임(`finishAnimationFrame`, 게임 스레드)에 몸 소켓에
  붙은 부품의 자리를 고친다. 할 일이 있다고 말하지 않으므로 몸이 쉬면 비용이 없다. 본 배열은 `CharacterPoseUtil`(`Engine/Character`)이 옮긴다.
- **질의**: `findSocketWorldTransform( "MainHand.Muzzle" )`(지난 프레임의 포즈 · 부품 월드 — 틱 안에서 읽기만), `findBindSocketTransform( "Eyes" )`(바인드 포즈 —
  흔들리지 않는 눈높이).
- **염색**: 해석의 머티리얼 값 중 `Parameter`(꾸미기 `MaterialColor` · `MaterialScalar`)를 주인의 부품마다 메시 몫 머티리얼 인스턴스에 건다(몸은 주인이 빈 값).
  `DyeChannel` · `PaletteSwap` 은 그것을 읽는 머티리얼이 생기면 건다.
- 세운 부품은 판의 모습이라 상태 저장 전에 `despawnParts` 로 걷는다(끝날 때도 걷는다). 아직 없는 것: 병합 메시(`MeshMerger`) · 피팅 · 체형 모프 · 본 비율을
  GPU 로, 떨어져 나감 이벤트 → `SocketBindingComponent` ReleasedPhysics, 데이터 핫 리로드 → 다시 조립, 같은 해시의 NPC 가 결과 나눠 쓰기.

## 형상 쪽(`Engine/Character`)이 쓰는 것

- `ResolvedAppearance::_listPart` — 부품마다 주인 · 아이템 · 에셋(변형 · 규칙 반영) · 머티리얼 · 스켈레톤 · 소켓 에셋 · 배치(소켓 후보 + 오프셋 · 회전) ·
  `_bDeforms` · 상태 · 피해 단계. `_listSocketSource` 로 소켓 이름 공간을 짓고(주인 이름 앞머리), `_listSocketOverride` 를 외형 층으로 덮는다.
- `_listMorph`(주인 비면 몸) · `_listBoneProportion` · `_listMaterialValue`(매개변수 · 염색 채널 · 팔레트) · `_listHiddenRegion`(몸 영역 이름 —
  `finishLoad( items, &몸영역이름 )` 으로 피팅 데이터의 영역 표와 대조할 수 있다) · `_listAttachment`(꾸미기 부착물) · `_listDetachedPart`.
- 해시가 같으면 병합 결과를 나눠 쓴다(`_meshHash` 가 병합 키, `_hash` 가 전체 키).
