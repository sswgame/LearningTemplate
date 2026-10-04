# UserSettings — 플레이어 옵션 메뉴의 백엔드

화면 · 그래픽 · 오디오 · 조작 · 게임플레이 · 접근성 · 언어 설정을 **데이터 스키마**로 정의하고, 값을 보류 · 적용 · 되돌리기 · 저장하며,
메뉴 UI 가 부를 바인딩 API 를 줍니다. 2D · 3D 를 가리지 않습니다 — 차원에 매인 설정은 없습니다.

| 파일 | 하는 일 |
|---|---|
| `UserSettingsSchema.*` | 스키마(카테고리 · 설정 · 품질 묶음 · 버전 단계)와 XML 로더 · 검사 · 값 정규화 |
| `UserSettingsRegistry.*` | 이름 → 적용기 · 선택지 공급자 등록부(데이터가 이름으로 고른다) |
| `UserSettingsEngineAppliers.*` | 엔진 적용기(오디오 버스 · 입력 · 언어 · 화면)와 화면 요청 `DisplaySettingsRequest` |
| `UserSettingsManager.*` | 엔진 서비스 — 값 상태 · 사용자 파일 · 적용/되돌리기 · 확인 카운트다운 · 키 바인딩 겹침 · 변경 통보 |
| `UserSettingsVariables.*` | 설정이 값을 넣는 전역 변수(`gv_renderScale` · `gv_colorVisionMode` …) |
| `HardwareProbe.*` | 첫 실행 품질 자동 선택용 사양(CPU 논리 코어 · 시스템 메모리) |

호스트 쪽: `Source/App/UserSettingsHost.*`(카운트다운 진행 · 화면 변경 적용 · `-gv_userSettingsApply`), 에디터: `Source/Editor/Panels/UserSettingsPanel.*`.

## 데이터

- 엔진 스키마 `Resource/engine/settings/engine.settings.xml`(경로는 `EngineDefaultAssets::_userSettingsSchema`).
- 게임 스키마는 게임 프리셋 `Config/Game/<게임>.json` 의 `_userSettingsSchema`(팩 상대)로 덧붙이고, 기본값은 `_mapUserSettingDefault` 로 바꿉니다.
  예: `Resource/game/shooter3d/data/shooter3d.settings.xml`(난이도) + `"_mapUserSettingDefault": { "gameplay.fieldOfView": "80" }`.
- 스키마 파일은 `*.settings.xml` 이고 `ResourceDataSchemaTest` 가 읽어 검사합니다.

```xml
<UserSettingsSchema version="1">
  <Category id="display" text="settings.category.display"/>
  <Setting id="display.windowMode" category="display" type="enum" default="windowed" confirmSeconds="15" target="applier:display.windowMode">
    <Option value="windowed"/><Option value="borderless"/>
  </Setting>
  <Setting id="graphics.renderScale" category="graphics" type="float" default="1" min="0.5" max="1" step="0.05"
           target="gv:gv_renderScale" enabledWhen="graphics.upscaler=off"/>
  <Setting id="audio.voiceVolume" category="audio" type="float" default="1" min="0" max="1" apply="immediate"
           target="applier:audio.busVolume" param="voice"/>
  <Setting id="controls.jump" category="controls" type="keyBinding" action="Jump" bindIndex="0" default=""/>
  <Scalability setting="graphics.quality" custom="custom" autoDetectFallback="medium">
    <Preset name="low"><Value setting="graphics.renderScale" value="0.75"/> ...</Preset>
    <AutoDetect preset="high" minCores="8" minMemoryMb="15000"/>
  </Scalability>
  <Upgrade version="1" op="rename" key="audio.master" to="audio.masterVolume"/>
</UserSettingsSchema>
```

- `type`: `bool` · `int` · `float` · `enum` · `keyBinding` · `string`. `apply`: `immediate`(보류 즉시 미리 적용) · `confirm`(기본, 적용 버튼) · `restart`(다음 실행).
- `target`: `gv:<전역 변수>` · `applier:<이름>` · 없음(게임이 `getValue` · 변경 통보로 읽는다). 키 바인딩은 대상을 적지 않습니다(`input.keyBinding`).
- `enabledWhen="a=b;c!=d"`, `platforms="windows linux"`, `optionsFrom="localization.languages"`, `confirmSeconds`.
- 품질 묶음의 기본 프리셋 값이 묶인 설정의 기본값입니다(두 곳에 적은 기본값이 어긋나 첫 화면이 Custom 이 되지 않게).
- **모르는 원소 · 속성 · 타입 · 적용기 · 전역 변수 · 의존 대상 · 범위 밖 기본값은 로드 오류**입니다. 적용기가 받는 값을 아는 경우(창 방식 이름 · 해상도 형식)
  선택지도 로드 때 대조합니다(`UserSettingValueFilterDelegate`).

## 사용자 파일

`%LOCALAPPDATA%/SWEngine/<팩>/usersettings.json`(Linux `$XDG_CONFIG_HOME` 또는 `~/.config/swengine/<팩>/`) — 세이브 게임과 별개.
자동화는 `-gv_userSettingsFile=<경로>` 로 사용자 폴더를 건드리지 않습니다.

```json
{ "version": 1, "values": { "display.resolution": "1600x900", "display.vsync": true, "graphics.renderScale": 0.75 } }
```

- **기본값과 다른 값만** 씁니다 — 다음 판에서 기본값이 바뀌면 바꾸지 않은 플레이어는 따라갑니다.
- **배포된 플레이어 데이터**입니다. 게임 데이터와 달리 다시 쓸 수 없으므로, 설정 id · 선택지 이름을 바꾸면 스키마의 `version` 을 올리고
  `<Upgrade>`(`rename` · `remove` · `mapValue` · `scale`)를 더합니다 — 저장소의 "별칭 없이 데이터를 다시 쓴다" 규칙의 예외입니다.
- 읽을 때 모르는 키는 경고와 함께 버리고, 범위 밖 값은 맞춰 넣고(경고), 형식이 틀린 값은 기본값으로 둡니다.
- 파일이 없으면 첫 실행 — `HardwareProbe` 로 품질 프리셋을 고릅니다(저장은 플레이어가 적용할 때).
- 확인 대기 중에 쓰는 파일은 옛 값입니다 — 확인 전에 꺼지면 다음 실행은 확인된 화면으로 뜹니다.

## 흐름

1. 기동 단계 `UserSettings`(Headless 뒤, RHI 앞): 적용기 등록 → 엔진 + 게임 스키마 → 게임 기본값 → 사용자 파일(없으면 자동 선택) → `reapplyAll`.
2. `RHI` 단계가 화면 요청을 읽어 창 크기 · 창 방식 · VSync 를 정합니다: EngineConfig → 플레이어가 고른 값(기본값이 아닌 것) → 명령줄.
3. `GameInstanceBase::initialize` 가 `onInitialize` 뒤에 `reapplyAll` — 언어 팩 · 입력 맵(키 바인딩 · 토글)이 그때 생깁니다.
   액션을 더 늦게 만드는 게임은 그 뒤에 `reapplyAll` 을 부릅니다. `restart` 설정은 첫 `reapplyAll` 에서만 바뀝니다.
4. 실행 중: 메뉴가 `setPendingValue` → `applyPending`. 화면 설정 적용기는 요청만 쌓고, App 이 프레임 맨 앞(OS 리사이즈와 같은 자리)에서
   렌더 스레드를 기다린 뒤 `IRHIDevice::setVSync` · `IWindow::setDisplayMode` 를 합니다. 창 크기 통보는 `App::onResize` 한 길로 스왑체인을 바꿉니다.
5. `confirmSeconds` 가 있는 값을 바꾸면 확인 대기 — `UserSettingsHost` 가 `update( dt )` 를 부르고, 시간 안에 `confirmChanges` 가 없으면 되돌려 적용 · 저장합니다.

## 메뉴 UI 가 부르는 것

```cpp
UserSettingsManager& settings = *game::getService<UserSettingsManager>();   // 엔진 · 에디터는 engine::getUserSettingsManager()
for ( const UserSettingCategoryDef& category : settings.getCategories() )   // 탭: category._textKey 로 로컬라이즈
    settings.collectSettings( category._id, listSetting );                  // 줄: def._textKey · _type · _minValue/_maxValue/_step
settings.getValue( id );                // 보일 값(보류 우선) — getBoolValue / getIntValue / getFloatValue
settings.collectOptions( id, list );    // 열거형 선택지(option._textKey)
settings.isSettingEnabled( id );        // 회색 처리(enabledWhen · 플랫폼)
settings.setPendingValue( id, text );   // UserSettingSetResult: Accepted · Clamped · Unchanged · Rejected · Disabled · Conflict
settings.findBindingConflict( id, "Key.F", conflict ); settings.setPendingBinding( id, "Key.F", UserSettingBindingPolicy::Swap );
settings.getBindingGlyph( id, InputGlyphStyle::GamepadXbox );
settings.applyPending();                // 결과: _bAwaitingConfirm(→ "유지할까요? N 초" 창) · _bNeedsRestart(→ "다시 시작 필요")
settings.revertPending(); settings.resetCategoryToDefaults( categoryId );
settings.isAwaitingConfirm(); settings.getConfirmSecondsLeft(); settings.confirmChanges();
settings.registerEventListener( SW_DELEGATE_METHOD( UserSettingEventListener, &Menu::onSettingEvent, this ) ); // 모듈 언로드 때 자동으로 떼어진다
```

## 주의

- 전역 변수 대상(`UserSettingsVariables.h`)은 Engine.dll 안의 변수입니다. Engine 안의 코드는 `extern` 으로 읽고, 게임 · 키트 모듈 · 시험은
  `engine::getGlobalVariableManager().findVariable( "gv_cameraShakeScale" )` 로 읽습니다(DLL 을 넘는 `extern` 은 링크되지 않습니다).
- 지금 아무도 읽지 않는 대상: `gv_renderScale` · `gv_upscaler` · 그림자 · 시야 거리 · 후처리 · 텍스처 · 이펙트 품질 · 모션 블러 · `gv_colorVisionMode`(톤맵 셰이더 미구현) ·
  UI 배율 · 자막. 오디오 버스 `voice` · `ambient` · `ui` 는 `IAudioSystem::setBusVolume` 에 값만 남습니다(재생 API 가 버스를 받지 않음).
- 해상도 선택지는 데이터의 고정 목록입니다(모니터 모드 열거 없음). 전용 전체 화면은 없고 `borderless`(모니터를 덮는 창)입니다.
- 키 바인딩의 빈 값은 "입력 맵의 기본 바인딩" 입니다 — 카테고리 기본값으로 되돌리면 지난 리바인딩이 남지 않습니다.
