# UI 문서와 데이터 바인딩

## 이것은 무엇이고 왜 있나

게임 화면을 C++ 코드로 만들면 버튼 위치 하나를 옮길 때마다 다시 빌드해야 하고, 디자이너가 직접 고칠 수 없습니다.
그래서 이 엔진은 화면을 데이터로 만듭니다. 구조는 문서(`*.ui.xml`), 겉모습은 스타일 시트(`*.uistyle.xml`), 움직임은 문서 안의 애니메이션이 정하고,
게임 상태는 뷰모델(`UiViewModel`)을 거쳐 바인딩 식으로 위젯에 들어갑니다. 실행 중에 파일을 고치면 화면이 바로 다시 만들어집니다.

이 문서는 [UI](../README.md)의 하위 문서입니다. 위젯 트리, 무효화 이유, 화면 스택은 UI 문서에서 먼저 읽으세요.
문서 하나로 화면을 띄우고 뷰모델을 연결하는 전체 흐름은 UI 문서의 "따라 해 보기"에 있습니다.
폴더로는 `Document/`, `Style/`, `Binding/`, `Animation/` 을 다룹니다.

## 작동 원리

### 문서

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `*.ui.xml`, `UiDocumentLoader` | UMG 위젯 블루프린트 | UXML, `VisualTreeAsset` | `.tscn` |
| `UserWidget` 조각 | 블루프린트 안의 위젯 | UXML `Template` | 씬 인스턴스 |
| `_command` → `onCommand` | 버튼 `OnClicked` 바인딩 | `clicked` 연결 | 시그널 연결 |

화면은 데이터로 만듭니다. 구조는 문서, 겉모습은 스타일 시트로 나누는데, 유니티의 UXML과 USS의 구분과 같습니다. 두 형식 모두 엔진 XML 하나입니다.
문서의 형식은 "따라 해 보기" 1단계와 같습니다. 원소는 위젯 타입이고, 속성은 PROPERTY 이며, 구조체 필드(`_slot`)는 자식 원소입니다. 위젯 타입인 자식 원소는 자식 위젯입니다.

- **읽기.** `UiDocumentLoader::parse` 가 파일마다 한 번 읽고, `UiDocumentCache` 가 경로로 보관합니다. 씬 파일과 같은 `XmlSerializer` 를 씁니다. 문서 캐시와 스타일 시트 캐시는 공통 몸체 `UiTextAssetCache`(메모리 글 → 리소스 → 절대 경로, 실패한 다시 읽기는 옛 에셋 유지)를 파생하고 파서만 다릅니다.
  모르는 원소, 속성, 열거자, 읽지 못한 값, 위젯 타입이 아닌 원소, 패널이 아닌 위젯 안의 자식은 모두 **로드 오류**입니다. 오류 문구는 `<경로>:<줄>: <이유>` 형식입니다.
  옛 형식을 읽는 코드는 없습니다(`_schemaVersion` 1).
- **바인딩 식.** `{` 로 시작하는 속성 값은 값으로 읽지 않고 `UiBindingDesc` 로 따로 보관합니다. 구조체 필드 안에서도 같습니다.
  `UiBindingDesc` 에는 위젯 번호, 프로퍼티 경로(예: `_slot._widthOverride`), 식 원문, 줄 번호가 있습니다. 화면이 이것을 보관하고(`UiScreen::getBindings`), 바인딩 단계가 연결합니다.
- **생성.** `UiDocumentLoader::instantiate` 가 화면을 열 때마다 위젯을 리플렉션 기본 생성자로 만듭니다.
  위젯 타입은 `Widget` 만 상속하는 단일 상속 사슬이어야 하고, `getTypeInfo()` 를 재정의해야 합니다. 아니면 생성 오류입니다.
- **조각.** `UserWidget` 은 `_document` 문서의 루트 위젯을 자식으로 끼우고, 조각 안의 이름을 `"<조각 위젯 이름>.<안쪽 이름>"` 으로 바꿉니다.
  같은 조각을 두 번 써도 `A.Label`, `B.Label` 로 구분되고, 조각 안의 조각은 `C.Inner.Label` 이 됩니다. 조각이 돌고 돌아 자기를 다시 부르면 생성 오류이고, `UserWidget` 원소에 자식 위젯을 적으면 로드 오류입니다.
- **명령.** 버튼의 `_command` 는 화면의 `onCommand( 이름, 위젯 )` 으로 갑니다. 기본 구현은 `registerCommand` 로 등록한 함수를 부르고, C++ 화면 클래스는 재정의합니다.
  아무도 처리하지 않은 명령은 경고 한 줄을 남깁니다. 문서의 명령 이름 오타가 조용히 묻히지 않게 하기 위해서입니다.
- **열기.** `UiSystem::openScreen( 경로 )` 가 문서의 `UiScreenDesc` 로 화면을 만들어 엽니다. 실패하면 무효 핸들을 돌려주고 오류를 로그에 남깁니다.
- **쓰기.** `UiDocumentWriter::write` 는 에디터 미리보기의 저장 기능입니다. 읽고 다시 쓰면 같은 문서가 나옵니다.
  기본값과 같은 필드는 쓰지 않습니다. 기본값인지는 위젯 타입의 기본값 인스턴스와 글로 비교해 판단합니다. 조각은 원소만, 바인딩 식은 식 그대로 씁니다.
  속성 순서는 리플렉션 순서(기반 타입 먼저)이므로, 손으로 쓰는 문서도 그 순서로 적습니다.
- **데이터 검사.** `ResourceDataSchemaTest` 가 저장소의 모든 `*.ui.xml` 을 읽어 위젯 트리까지 만듭니다.
  번역할 글 수집은 엔진 현지화 프로젝트의 `assetRoots` 에 `engine/ui` 가 있어서 이루어집니다. `TextWidget::_text` 가 `Meta = "Localizable"` 이고, 바인딩 식은 수집하지 않습니다.
- 문서 캐시는 `UiSystem` 이 소유하고, 기동 단계 `Ui` 가 에셋 캐시 레지스트리에 종류 `UiDocument` 로 등록합니다.

### 스타일과 테마

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `*.uistyle.xml`, 선택자, 특정도 | Slate 스타일 세트, UMG 스타일 에셋 | USS | `Theme` |
| `WidgetStyle`, `UiComputedStyle` | `FSlateWidgetStyle` | `resolvedStyle` | 테마 덮어쓰기 |
| `UiThemeCatalog`, `UiSystem::setTheme` | 스타일 세트 교체 | 테마 스타일 시트(TSS) | `Control.theme` |

<!-- snippet: 스타일 시트 예(변수, 상태 선택자, 자손 결합자, 구조체 필드) — 5b U7 에서 Resource/engine/ui/styles/default.uistyle.xml 구간과 대조 -->
```xml
<UiStyleSheet _schemaVersion="1">
	<Variable _name="accent" _value="0.25,0.5,1,1" />                          <!-- 이 시트의 $변수, 위치는 어디든 -->
	<Rule _selector="ButtonWidget.primary:hover" _backgroundColor="$accent" />  <!-- 속성 이름 = WidgetStyle PROPERTY -->
	<Rule _selector="BorderPanel.window TextWidget" _textColor="0.92,0.93,0.95,1" />
	<Rule _selector="TextWidget.title" _fontSize="32">
		<_font _weight="Bold" />                                                <!-- 구조체 필드는 자식 원소, 적은 필드만 -->
	</Rule>
</UiStyleSheet>
```

- **필드**(`WidgetStyle`)는 배경색, 모서리, 테두리 두께와 색, 그림자 색과 오프셋과 흐림, 여백, 글꼴, 글자 크기, 글 색, 외곽선 색과 두께, 포커스 테두리 색, 불투명도, 전환입니다.
  필드 번호(`UiStyleField`)와 필드 정보 테이블(`UiStyleFieldTable`)은 PROPERTY 순서와 짝을 이룹니다(`UiStyleTest.FieldTableMatchesReflection`).
  테이블에는 레이아웃에 영향을 주는지, 자손 그리기 캐시에 반영되는지, 상속되는지가 적혀 있습니다.
- **선택자**는 `타입? .클래스* #이름? :상태*` 단위를 공백(자손 결합자, 아무 조상)으로 연결한 것입니다. 타입은 **정확한 타입**만 맞고, 파생 타입은 맞지 않습니다. USS와 같습니다.
  상태는 `hover`, `pressed`, `focus`, `focus-visible`, `disabled`, `checked`, `selected` 입니다. `focus-visible` 은 포커스와 탐색 입력 방식이 함께일 때이고, `disabled` 는 자기나 조상이 꺼졌을 때입니다.
  위젯이 `Widget::computeStyleStates` 로 자기 상태를 답합니다. 버튼은 누름을, 체크 상자는 켜짐을 더합니다. `>`, `+`, `*`, `[속성]` 은 지원하지 않으며 로드 오류입니다.
- **특정도**는 (#이름 수, .클래스와 :상태 수, 타입 수)를 사전 순으로 비교합니다. 같으면 테마 시트 다음 문서 시트 순서이고, 같은 시트 안에서는 뒤에 있는 규칙이 이깁니다.
- **상속.** 글 필드(글꼴, 크기, 글 색, 외곽선)는 부모가 정한 값을 물려받고 자기 규칙이 덮습니다. 나머지 필드는 규칙이 정한 것만 씁니다.
- **위젯 필드와의 관계.** 계산된 스타일은 "정한 필드" 비트를 보관합니다. 위젯은 **스타일이 정한 필드면 그 값, 아니면 자기 필드**를 씁니다.
  자기 필드는 코드 세터, 문서 속성, 버튼의 상태 브러시이고, 시트가 없는 화면의 기본 겉모습입니다. 겉모습을 데이터로 바꾸는 방법은 시트이고, 위젯 필드는 그 기본값입니다.
- **공유.** 계산된 스타일은 (맞은 규칙, 부모의 계산된 스타일)로 정해지므로, 조건이 같은 위젯은 객체 하나를 함께 씁니다. 버튼 100개가 있어도 계산된 스타일은 하나입니다.
- **스타일 패스**(`UiStylePass`)는 레이아웃 전에 실행하고, `kStyle` 인 위젯만 다시 맞춥니다. 상태, 클래스, 이름이 바뀌었거나 트리에 붙은 위젯입니다.
  자손으로는 계산된 스타일이 바뀌었거나, 그 위젯이 맞는 "조상 쪽 선택자 조각"이 바뀌었을 때만 내려갑니다. 그래서 호버 하나 때문에 트리 전체를 돌지 않습니다.
  바뀐 필드가 여백, 글꼴, 크기면 `kLayout`, 불투명도면 `kTransform`, 그 밖이면 `kPaint` 입니다.
- **테마**(`UiThemeCatalog`)는 테마 이름에서 시트 목록으로 가는 테이블입니다. `engine/ui/uithemes.xml` 을 게임 프리셋의 `_uiThemes` 가 덮어씁니다.
  모든 화면에 테마 시트, 그다음 문서와 조각의 시트를 적용합니다. `UiSystem::setTheme( 이름 )` 은 모든 화면을 다시 맞춥니다. 엔진 기본 테마 `default` 는 `engine/ui/styles/default.uistyle.xml` 입니다.
- 문서는 `<_listStyleSheet>` 로 자기 시트를 적용합니다. 조각 문서의 시트도 모입니다. 시트는 `UiStyleSheetCache` 가 경로로 보관하고, 에셋 종류는 `UiStyleSheet` 입니다.

### 핫 리로드

- 에디터의 파일 감시(`AssetHotReload`)가 `*.ui.xml` 과 `*.uistyle.xml` 이 바뀌면 그 경로를 보관한 캐시의 `reload` 를 부릅니다.
  캐시는 **다시 읽어 파싱에 성공하면** 교체하고 `UiSystem` 에 알립니다. 실패하면 **이전 것을 그대로 두고** 오류만 남깁니다.
  실패한 저장이 화면을 지우지 않게 하기 위해서이고, 현지화의 `reloadChangedFile` 과 같은 규칙입니다.
- 문서가 바뀌면 그 문서로 연 화면과, 조각으로 그 문서를 쓰는 문서로 연 화면의 트리를 새로 만들고 바인딩과 시트를 다시 적용합니다.
  포커스 위젯, 덮였을 때 기억한 포커스, 스크롤 패널 오프셋은 **이름으로** 이어 갑니다. 새 트리를 바로 한 번 스타일을 맞추고 계산한 뒤 돌려주므로, 오프셋은 내용 크기 안으로 제한됩니다.
  화면 성질(층, 모달)은 바꾸지 않습니다. 스택 순서가 흔들리지 않게 하기 위해서이고, 다음에 열 때 새 성질이 적용됩니다.
- 스타일 시트가 바뀌면 그 시트를 쓰는 화면의 스타일 세트만 다시 적용하고 위젯을 다시 맞춥니다. 트리와 위젯 번호는 그대로입니다.
- Shipping에는 파일 감시가 없을 뿐, 다시 읽는 경로는 캐시의 다시 읽기 함수 하나로 같습니다.

### 데이터 바인딩

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `UiViewModel`, `setField` | UMG MVVM, FieldNotify | 데이터 소스, 변경 추적 | 시그널 |
| `{bind:필드}`, `mode=TwoWay` | MVVM 바인딩(OneWay, TwoWay) | `bindingMode` | 시그널 양방향 연결 |
| `converter=` | 변환 함수 | `ConverterGroup` | (코드) |
| `{poll:필드}` | 옛 속성 바인딩 | `EveryUpdate` | `_process` 에서 읽기 |
| `{setting:id}` | Lyra `GameSettingRegistry` | (직접 구현) | `ProjectSettings` |

게임 상태를 위젯에 넣는 방법은 **뷰모델**입니다. 이 결정의 이유는 [결정 기록 5-7](../../../../docs/09_Decisions.md#5-7-런타임-ui-와-글자)에 있습니다.

```xml
<SliderWidget _value="{bind:_volume, mode=TwoWay}" />              <!-- 사용자가 움직이면 뷰모델에 되쓴다 -->
<TextWidget _text="{bind:_ammo, format=HUD.Ammo}" />               <!-- 현지화 패턴 "Ammo: {value}" -->
<TextWidget _text="{bind:_health, converter=Percent}" />           <!-- 0.75 → "75%" -->
<ProgressBarWidget _percent="{bind:_health}" />
```

- **뷰모델**(`UiViewModel`)의 필드는 리플렉션 PROPERTY 입니다. 파생 클래스는 `REFLECT()` 를 달고 `getTypeInfo()` 를 재정의합니다.
  기반은 `UiViewModel` 하나만 둡니다. 필드 오프셋이 객체 시작 기준이기 때문입니다. 세터는 `setField( _health, value, "_health" )` 로 쓰고, 같은 값이면 알리지 않습니다.
  알림은 단조 증가하는 번호로 남으므로, 한 프레임에 여러 번 알려도 위젯 필드는 한 번만 씁니다. 화면 여러 개가 뷰모델 하나를 함께 볼 수 있습니다.
  소유자는 게임입니다. `UiScreen::setViewModel` 로 연결하고, 뷰모델이 먼저 지워지면 바인딩이 그 뷰모델을 놓습니다.
- **바인딩 단계**는 `UiSystem::update` 의 레이아웃 전에 실행합니다. 화면마다 `UiBindingSet` 이 아직 연결되지 않았으면 식을 해석해 연결하고 모든 필드를 씁니다.
  이미 연결되어 있으면 알림이 온 필드에 연결된 위젯 필드만 리플렉션으로 씁니다. 쓴 뒤 위젯의 `onBoundPropertyChanged` 가 그 필드의 세터와 같은 무효화를 합니다.
  글과 크기 필드는 레이아웃을, 색 필드는 그리기만 다시 합니다. 필드 종류를 PROPERTY 메타로 적지 않는 이유는, 커스텀 메타가 Shipping에서 지워지기 때문입니다.
- **타입 검사는 연결할 때 합니다.** 값 종류(불리언, 숫자, 글, 그 밖)가 맞지 않고 변환기도 없으면 오류이고, 그 식은 연결되지 않습니다.
  오류 문구는 `<문서>:<줄>: binding '<식>' on <필드>: <이유>` 이고, 로그와 `getErrors` 에 남습니다.
  불리언과 숫자는 서로 바꾸고, 무엇이든 글로는 바꿉니다(리플렉션 글 표기). 글에서 숫자로는 바꾸지 않습니다. 색, 벡터, 열거형은 같은 타입끼리만 연결합니다.
- **양방향.** 슬라이더, 체크 상자, 콤보 상자, 글 입력 위젯이 사용자 입력 경로에서 `notifyValueEdited` 를 부르면 소스에 되쓰고 알립니다.
  그 알림으로 같은 위젯 필드에 다시 쓰지는 않고, 같은 필드의 다른 바인딩은 받습니다. 변환기나 형식이 있는 식은 양방향일 수 없습니다.
- **변환기**는 이름으로 찾습니다(`UiSystem::getBindingConverters`). 엔진 기본은 `Percent`, `Invert`, `NotEmpty`, `IsZero`, `Seconds`(m:ss)입니다.
  게임이 추가한 변환기는 그 모듈이 언로드될 때 제거되고, 화면은 바인딩을 다시 연결합니다.
- **폴링**(`{poll:필드}`)은 알림 없이 매 프레임 값을 비교하는 개발 편의 기능입니다. 비교한 수가 프로파일 카운터 `Ui.PollBindings` 에 보이고, Shipping에서 쓰면 경고 한 줄을 남깁니다.

#### 현지화 글

- 글 위젯의 `_text`(`Meta = "Localizable"`)는 **현지화 키 또는 글 그대로**입니다. 보이는 글은 측정하고 그릴 때 문맥의 문화권으로 `getStringByText( 키, 키 )` 를 불러 얻습니다.
  테이블에 없으면 글 그대로이므로, 문서에 영어 원문 `"Paused"` 를 적어도 그대로 보입니다. 해석한 글은 텍스트 리비전(`getTextRevision`)과 함께 위젯이 캐시합니다(`getDisplayText`).
- 바인딩 단계가 프레임마다 텍스트 리비전 정수 하나를 비교합니다. 바뀌면 글꼴 대체 사슬을 비우고(`FontSystem::invalidateFaceChains`) 모든 위젯에 `onTextRevisionChanged` 를 부릅니다.
  글 위젯은 글을 다시 해석하고 `kLayout` 이 됩니다. 언어가 바뀌면 글 길이가 바뀌기 때문입니다. 형식 바인딩(`format=`)도 같은 프레임에 다시 포맷합니다.
- 코드는 **키를 넣습니다**(`setText( "Menu.Start" )`). `SW_LOCTEXT` 로 미리 해석한 글을 넣으면 언어를 바꿔도 그대로입니다.
  플레이어가 입력한 글처럼 해석하면 안 되는 글은 `setLocalized( false )` 로 둡니다. 글 입력 위젯은 입력한 글은 해석하지 않고 힌트만 해석합니다.

#### 사용자 설정

`{setting:설정 id}` 는 `UserSettingsManager`(옵션 메뉴의 백엔드)를 소스로 씁니다. 옵션 행은 값을 바꾸는 것이 목적이므로 기본이 양방향입니다.

```xml
<SliderWidget _value="{setting:audio.master}" />          <!-- 값과 범위, 눈금(_minValue, _maxValue, _step)은 설정 정의에서 -->
<ComboBoxWidget _selectedIndex="{setting:video.mode}" />  <!-- 선택지(_listOption)는 선택지의 글 키, 없으면 값 -->
<CheckBoxWidget _bChecked="{setting:video.vsync}" />      <!-- enabledWhen 이 거짓이면 위젯을 끈다 -->
```

- **읽기**는 `getValue` 로 하고, 보류 값이 있으면 그것을 씁니다. 필드가 `_value` 이고 위젯에 `_minValue`, `_maxValue`, `_step` 이 있으면 설정 정의의 범위를 먼저 씁니다.
  필드가 `_selectedIndex` 이고 위젯에 `_listOption` 이 있으면 선택지를 먼저 씁니다. 필드 이름으로 판단하므로 슬라이더와 콤보 상자를 따로 알 필요가 없습니다.
  `isSettingEnabled` 결과는 위젯의 사용 가능 여부가 됩니다.
- **쓰기**(사용자 입력)는 설정 타입에 맞는 `setPending*Value` 를 부릅니다. 결과는 보지 않고, 다음 바인딩 단계가 설정 값을 다시 읽습니다.
  눈금에 맞춰 고쳐 받았으면(Clamped) 고친 값이, 거절됐으면(Rejected, Disabled) 원래 값이 보입니다. 키 바인딩 충돌(Conflict)은 키 바인딩 창(`KeyRebindScreen`)이 처리합니다.
- 다른 곳에서 바꾼 값(되돌리기, 기본값, 확인 카운트다운의 자동 되돌림)은 변경 통보(`registerEventListener`)로 받습니다.
  사용 가능 여부가 다른 설정에 좌우되므로, 통보가 오면 그 화면의 설정 바인딩을 모두 다시 읽습니다. 리스너는 바인딩 세트(화면)가 지워질 때 해제합니다.
  테스트는 `UiSystem::setUserSettings` 로 자기 매니저를 넘깁니다.

### 애니메이션과 스타일 전환

| 이 엔진 | 언리얼 | 유니티 UI Toolkit | Godot |
|---|---|---|---|
| `UiAnimation`, `UiAnimationPlayer` | UMG 위젯 애니메이션 | 애니메이션 클립, 코드 | `AnimationPlayer` |
| `UiSystem::tween` | 블루프린트 타임라인 | `experimental.animation` | `Tween` |
| 스타일 `_transition` | (없음) | USS `transition` | (없음) |

**움직임은 렌더 변환, 불투명도, 색으로 만듭니다.** 이 필드는 `kTransform` 과 `kPaint` 라서 레이아웃이 돌지 않습니다.
크기나 여백(`_slot._widthOverride` 등)을 움직이면 매 프레임 그 위젯부터 레이아웃 경계까지 다시 계산합니다(`UiAnimationTest.TransformTrackDoesNotRelayout`, `UiAnimationTest.SizeTrackRelayouts`).

<!-- snippet: 문서의 Open 애니메이션(트랙 하나, 키 둘, 사건 하나) — 5b U7 에서 Resource/engine/ui/pause.ui.xml 구간과 대조 -->
```xml
<UiDocument _schemaVersion="1">
	<_listAnimation>
		<UiAnimation _name="Open">                                          <!-- Open 과 Close 는 화면 스택이 재생한다 -->
			<_listTrack>
				<UiAnimationTrack _widget="Window" _property="_opacity">   <!-- 위젯 이름(조각 안이면 조각.이름)과 프로퍼티 경로 -->
					<_listKey>
						<UiAnimationKey _time="0" _value="0" _curve="Linear" />
						<UiAnimationKey _time="0.2" _value="1" _curve="EaseOut" /> <!-- 곡선은 이 키까지 가는 구간의 것 -->
					</_listKey>
				</UiAnimationTrack>
			</_listTrack>
			<_listEvent><UiAnimationEvent _time="0.2" _command="Opened" /></_listEvent>  <!-- 화면 onCommand 로 -->
		</UiAnimation>
	</_listAnimation>
	…
</UiDocument>
```

- **값.** 키의 `_value` 는 그 필드의 글 표기로, XML 속성과 같습니다. 실수, `float2`, `float3`, `float4` 필드는 키 사이를 곡선(`BlendCurve`)으로 보간합니다. 카메라와 시퀀서가 쓰는 곡선과 같습니다.
  정수, 불리언, 열거형, 글은 계단식으로 바뀝니다. 예를 들어 `_visibility` 를 `Collapsed` 에서 `Visible` 로 바꿉니다.
  프로퍼티 경로는 재생을 시작할 때 한 번 해석하고, 쓰기는 리플렉션으로 한 뒤 `Widget::onBoundPropertyChanged` 가 세터와 같은 무효화를 합니다. 바인딩과 같은 경로입니다.
  해석하지 못한 트랙(위젯, 경로, 값)은 경고하고 그 트랙만 뺍니다.
- **재생기**(`UiAnimationPlayer`)는 화면마다 하나입니다(`UiScreen::getAnimationPlayer`). `play( 이름, 배속, 반복 수 )` 의 반복 수 0은 끝없이 반복입니다.
  `playReverse`, `reverse`, `stop`, `isPlaying` 이 있고, `reverse` 는 재생 중이면 그 위치에서 방향을 바꿉니다.
  재생을 시작하면 첫 프레임 값을 바로 씁니다. 열린 화면이 한 프레임 동안 끝 값으로 번쩍이지 않게 하기 위해서입니다.
  사건은 재생이 그 시각을 지날 때 한 바퀴에 한 번, 진행이 끝난 뒤 보냅니다. 그래서 명령 처리 안에서 애니메이션을 재생하거나 화면을 닫아도 됩니다.
- **열기와 닫기.** 이름이 `Open` 인 애니메이션은 `pushScreen` 이, `Close` 는 `closeScreen` 이 재생합니다. 닫기는 `Close` 가 끝난 뒤 실제로 지우고, 그동안 화면은 그려지기만 합니다. 입력, 포커스, 활성 판정에서는 빠집니다.
- **트윈.** `UiSystem::tween( 위젯 번호, 경로, 끝값, 길이, 곡선 )` 은 지금 값에서 끝값까지 옮기는 코드 한 줄짜리 애니메이션입니다. Godot의 `Tween` 과 같습니다. 같은 위젯과 경로의 새 트윈은 이전 트윈을 대신합니다.
- **시간.** `UiSystem::update` 의 애니메이션 단계는 스타일과 레이아웃 전에 실행하고, 실제 프레임 시간으로 진행합니다. 그래서 게임이 멈춘 일시정지 메뉴도 움직입니다.
- **움직임 줄이기.** 사용자 설정 `accessibility.reduceMotion`(`gv_uiReduceMotion`)이 켜지면 애니메이션, 트윈, 닫기가 바로 끝 값이 됩니다. 사건은 모두 보냅니다.

**스타일 전환**(`Animation/UiStyleTransition`)은 USS와 CSS의 `transition` 에 해당합니다. 시트 규칙의 `_transition` 필드에 적습니다.

```xml
<Rule _selector="ButtonWidget" _transition="_backgroundColor 0.12 EaseOut, _opacity 0.2 Linear" />   <!-- 필드 길이(초) 곡선(없으면 EaseOut) -->
<Rule _selector="ButtonWidget:hover" _backgroundColor="$accentHover" />
```

- 상태, 클래스, 테마가 바뀌어 계산된 스타일이 바뀌면, 새 스타일의 `_transition` 에 적힌 필드만 **지금 보이는 값**에서 새 값으로 옮겨 갑니다. 스타일 패스가 그 프레임에 한 번 계산합니다.
  진행 중에 또 바뀌면 그 위치에서 새 목표로 향하고, 처음 값으로 튀지 않습니다. 이후 프레임에는 애니메이션 단계가 값만 보간하고, 필드 종류에 따라 `kPaint`, `kLayout`, `kTransform` 이 됩니다.
  `all` 은 보간되는 모든 필드이고, 뒤 항목이 앞 항목을 덮습니다.
- 보간되는 필드는 실수 필드입니다. 색, 모서리, 두께, 그림자, 여백, 글자 크기, 외곽선, 불투명도가 해당합니다.
  글꼴, `_transition` 자신, 모르는 필드, 읽지 못한 길이, 모르는 곡선은 시트 로드 오류입니다.
- 바로 바뀌는 경우도 있습니다. `_transition` 에 적지 않은 필드, 처음 스타일을 맞추는 위젯, 이전과 새 스타일 중 한쪽만 정한 필드, `gv_uiReduceMotion` 이 켜진 경우입니다.
- 보이는 값은 위젯이 보관하는 복사본(`UiStyleTransitionState`)이고, `Widget::getComputedStyle` 이 그것을 돌려줍니다. 공유하는 계산된 스타일은 바꾸지 않고, 전환이 끝나면 복사본을 지웁니다.
- 상속되는 글 필드는 부모의 **목표** 값을 물려받습니다. 보간 중인 값을 자손에 내리면 공유 키가 프레임마다 바뀌기 때문입니다.
  자식 글도 함께 움직이게 하려면 자식 규칙에도 `_transition` 을 적습니다(예: `ButtonWidget TextWidget`). 자식 입장에서는 물려받은 값이 바뀐 것도 바뀐 필드입니다.

### 오프스크린 화면

에디터의 UI 미리보기 패널(`UiPreviewPanel`)이 쓰는 기능입니다.

- `UiSystem::openOffscreenScreen( 문서, 렌더 텍스처 경로 )` 는 문서를 화면 스택 **밖**에서 만듭니다. 입력, 포커스, 활성 화면, 게임 정지와 상관없습니다.
  `setOffscreenView( 핸들, 뷰포트, 글자 배율, 테마 )` 로 이 화면에만 뷰포트(UI 크기, 배율, 안전 영역)와 테마를 정합니다. 게임 UI의 테마는 그대로입니다.
- `update` 끝에 화면마다 스타일, 레이아웃, 그리기를 자기 뷰포트로 실행합니다. 그리기 목록은 `collectWorldCanvases` 가 렌더 텍스처 대상(불투명 바탕)으로 내보냅니다.
  렌더러는 월드 위젯과 같은 경로로 그리고, 셰이더 읽기 상태로 둡니다. 렌더 텍스처 크기는 처음 만들 때 정해지므로, 크기가 다르면 경로를 바꿉니다.
- 문서와 스타일 시트의 핫 리로드는 스택의 화면과 같은 경로(`onDocumentReloaded`, `onStyleSheetReloaded`)로 미리보기도 다시 만듭니다.
- 애니메이션 단계는 실행하지 않습니다. `Open` 을 재생하지 않으므로 문서에 적힌 값 그대로 보입니다. Open의 첫 키가 투명이어도 미리보기가 비지 않는 이유입니다(`UiDocumentTest.OffscreenScreenDoesNotPlayOpenAnimation`).
  스타일 전환은 시작한 프레임에 끝 값으로 맞춥니다(`UiStyleTest.OffscreenScreenFinishesTransitionsAtOnce`).
- 미리보기 패널은 그 텍스처를 ImGui 이미지로 보여 주고, 레이아웃 사각형과 고른 위젯, 안전 영역은 ImGui 선으로 그 위에 그립니다. 캔버스는 바꾸지 않습니다.

## 확장하는 법

### 바인딩 변환기 추가하기

`UiSystem::getBindingConverters().registerConverter( UiBindingConverter{ 이름, 함수, 입력 종류, 출력 종류 } )` 로 등록합니다.
입력과 출력 종류는 연결할 때 타입 검사에 씁니다. 게임 모듈이 등록한 변환기는 모듈 언로드 때 제거됩니다.

## 함정과 주의

**레이아웃을 움직이는 애니메이션을 기본으로 쓰지 않습니다.** 크기와 여백을 움직이면 매 프레임 레이아웃이 다시 돕니다. 렌더 변환, 불투명도, 색으로 움직입니다.

**뷰모델을 화면보다 먼저 지울 때는 분리부터 합니다.** 닫기는 지연되고 닫기 애니메이션도 있으므로, `closeScreen` 전에 `setViewModel( nullptr )` 을 부릅니다.

**문서 속성 순서를 바꿔 쓰지 않습니다.** 문서 쓰기는 리플렉션 순서(기반 타입 먼저)로 쓰므로, 손으로 쓸 때도 같은 순서로 적어야 에디터 저장 뒤 diff가 작습니다.

**PROPERTY 커스텀 메타로 동작을 정하지 않습니다.** 커스텀 메타는 Shipping에서 지워집니다. 바인딩의 필드 종류 판단이 메타 대신 위젯의 `onBoundPropertyChanged` 를 쓰는 이유입니다.

## 더 볼 곳

- [UI](../README.md): 위젯 트리, 무효화, 화면 스택, 따라 해 보기
- [엔진 기본 화면과 접근성](../Screen/README.md): 이 문서의 기능으로 만든 엔진 기본 화면
- [Localization](../../Localization/README.md): 현지화 키와 문화권
- [결정 기록 5-7](../../../../docs/09_Decisions.md#5-7-런타임-ui-와-글자): 뷰모델과 엔진 XML을 고른 이유

| 파일 | 여는 때 |
|---|---|
| `UiDocumentLoader.h` | 문서 읽기와 위젯 생성 규칙 |
| `../Style/UiStyleSheet.h` | 선택자와 특정도 |
| `../Binding/UiViewModel.h` | 뷰모델 필드와 알림 |
| `../Animation/UiAnimationPlayer.h` | 재생기 API |
