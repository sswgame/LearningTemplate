/**
 * @file ReflectionAnnotationKeys.h
 * @brief 리플렉션 어노테이션 인자 키를 편집기에 알리는 선언입니다. **생성 파일 — 고치지 마십시오.**
 * @details `Scripts/generate/GenerateReflectionAnnotationKeys.py` 가 `AnnotationMeta.txt`(키와 철자), `ReflectUnits.h`(단위),
 *          `PredefinedContainerKind.xxx`(컨테이너 종류)에서 만듭니다. 키를 더하려면 그 파일들을 고치고 스크립트를 다시 실행합니다.
 *          편집기에서만 include 됩니다(`ReflectionIntellisense.h`). 빌드와 ReflectionParser 는 이 파일을 읽지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::reflect::attr
{
    /** @brief 키 하나. `Key = 값` 의 값은 무엇이든 받습니다(값 형식은 ReflectionParser 가 검사합니다). */
    struct AnnotationKey
    {
        /** @brief `Key = 값` 을 식으로 받습니다. */
        template <typename T>
        constexpr const AnnotationKey& operator=( T&& ) const noexcept
        {
            return *this;
        }
    };
} // namespace sw::reflect::attr

namespace sw::reflect::attr
{
    /** @brief `REFLECT( … )` 의 키입니다. */
    struct ReflectKeys
    {
        /** @brief flag.Abstract — 단독 토큰 `Abstract` 또는 `Abstract = true`. 같은 필드의 철자: Abstract, abstract. */
        static constexpr AnnotationKey Abstract{};
        /** @brief flag.Abstract — 단독 토큰 `abstract` 또는 `abstract = true`. 같은 필드의 철자: Abstract, abstract. */
        static constexpr AnnotationKey abstract{};
        /** @brief flag.Static — 단독 토큰 `Static` 또는 `Static = true`. 같은 필드의 철자: Static, static. */
        static constexpr AnnotationKey Static{};
        /** @brief flag.HideInMenu — 단독 토큰 `HideInMenu` 또는 `HideInMenu = true`. 같은 필드의 철자: HideInMenu, HiddenInMenu, EditorHidden. */
        static constexpr AnnotationKey HideInMenu{};
        /** @brief flag.HideInMenu — 단독 토큰 `HiddenInMenu` 또는 `HiddenInMenu = true`. 같은 필드의 철자: HideInMenu, HiddenInMenu, EditorHidden. */
        static constexpr AnnotationKey HiddenInMenu{};
        /** @brief flag.HideInMenu — 단독 토큰 `EditorHidden` 또는 `EditorHidden = true`. 같은 필드의 철자: HideInMenu, HiddenInMenu, EditorHidden. */
        static constexpr AnnotationKey EditorHidden{};
        /** @brief string.Alias — `Alias = "…"`. 직렬화 · 핫 리로드용 옛 타입 이름. 여러 개면 Alias="OldA, OldB" */
        static constexpr AnnotationKey Alias{};
        /** @brief string.Category — `Category = "…"`. */
        static constexpr AnnotationKey Category{};
        /** @brief string.DisplayName — `DisplayName = "…"`. */
        static constexpr AnnotationKey DisplayName{};
        /** @brief string.Tooltip — `Tooltip = "…"`. */
        static constexpr AnnotationKey Tooltip{};
        /** @brief string.Meta — `Meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey Meta{};
        /** @brief string.Meta — `meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey meta{};
        /** @brief string.Meta — `CustomMeta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey CustomMeta{};
        /** @brief string.Validate — `Validate = "…"`. 타입 검증 함수 — 같은 타입의 void fn( ValidationContext& context ) [const]. 로드 · 저장 · 인스펙터 편집 뒤에 불린다(ReflectionValidation) */
        static constexpr AnnotationKey Validate{};
    };

    inline constexpr const utf8* kArrReflectKey[] = {
        "Abstract",
        "abstract",
        "Static",
        "static",
        "HideInMenu",
        "HiddenInMenu",
        "EditorHidden",
        "Alias",
        "Category",
        "DisplayName",
        "Tooltip",
        "Meta",
        "meta",
        "CustomMeta",
        "Validate",
        nullptr,
    };
} // namespace sw::reflect::attr

namespace sw::reflect::attr
{
    /** @brief `ENUM( … )` 의 키입니다. */
    struct EnumKeys
    {
        /** @brief string.Alias — `Alias = "…"`. */
        static constexpr AnnotationKey Alias{};
        /** @brief string.ValueAlias — `ValueAlias = "…"`. 같은 필드의 철자: ValueAlias, EnumeratorAlias. 열거자 이름 변경: ValueAlias="OldIdle:Idle, OldMoving:Moving" */
        static constexpr AnnotationKey ValueAlias{};
        /** @brief string.ValueAlias — `EnumeratorAlias = "…"`. 같은 필드의 철자: ValueAlias, EnumeratorAlias. 열거자 이름 변경: ValueAlias="OldIdle:Idle, OldMoving:Moving" */
        static constexpr AnnotationKey EnumeratorAlias{};
        /** @brief string.Meta — `Meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey Meta{};
        /** @brief string.Meta — `meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey meta{};
        /** @brief string.Meta — `CustomMeta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey CustomMeta{};
        /** @brief flag.Flags — 단독 토큰 `Flags` 또는 `Flags = true`. 같은 필드의 철자: Flags, BitFlag, FLAG, Bitwise. 비트 플래그 연산자(|, &, ^, ~)를 만든다. 직렬화용 EnumInfo::_bIsBitFlag 도 켠다. */
        static constexpr AnnotationKey Flags{};
        /** @brief flag.Flags — 단독 토큰 `BitFlag` 또는 `BitFlag = true`. 같은 필드의 철자: Flags, BitFlag, FLAG, Bitwise. 비트 플래그 연산자(|, &, ^, ~)를 만든다. 직렬화용 EnumInfo::_bIsBitFlag 도 켠다. */
        static constexpr AnnotationKey BitFlag{};
        /** @brief flag.Flags — 단독 토큰 `FLAG` 또는 `FLAG = true`. 같은 필드의 철자: Flags, BitFlag, FLAG, Bitwise. 비트 플래그 연산자(|, &, ^, ~)를 만든다. 직렬화용 EnumInfo::_bIsBitFlag 도 켠다. */
        static constexpr AnnotationKey FLAG{};
        /** @brief flag.Flags — 단독 토큰 `Bitwise` 또는 `Bitwise = true`. 같은 필드의 철자: Flags, BitFlag, FLAG, Bitwise. 비트 플래그 연산자(|, &, ^, ~)를 만든다. 직렬화용 EnumInfo::_bIsBitFlag 도 켠다. */
        static constexpr AnnotationKey Bitwise{};
        /** @brief string.Invalid — `Invalid = "…"`. 같은 필드의 철자: Invalid, InvalidValue. TypeRegistry::enumToString / enumFromString 이 쓰는 Invalid 센티널과 (배타적) Count 센티널 */
        static constexpr AnnotationKey Invalid{};
        /** @brief string.Invalid — `InvalidValue = "…"`. 같은 필드의 철자: Invalid, InvalidValue. TypeRegistry::enumToString / enumFromString 이 쓰는 Invalid 센티널과 (배타적) Count 센티널 */
        static constexpr AnnotationKey InvalidValue{};
        /** @brief string.Count — `Count = "…"`. 같은 필드의 철자: Count, CountValue. */
        static constexpr AnnotationKey Count{};
        /** @brief string.Count — `CountValue = "…"`. 같은 필드의 철자: Count, CountValue. */
        static constexpr AnnotationKey CountValue{};
    };

    inline constexpr const utf8* kArrEnumKey[] = {
        "Alias",
        "ValueAlias",
        "EnumeratorAlias",
        "Meta",
        "meta",
        "CustomMeta",
        "Flags",
        "BitFlag",
        "FLAG",
        "Bitwise",
        "Invalid",
        "InvalidValue",
        "Count",
        "CountValue",
        nullptr,
    };
} // namespace sw::reflect::attr

namespace sw::reflect::attr
{
    /** @brief `PROPERTY( … )` 의 키입니다. */
    struct PropertyKeys
    {
        /** @brief flag.ReadOnly — 단독 토큰 `ReadOnly` 또는 `ReadOnly = true`. */
        static constexpr AnnotationKey ReadOnly{};
        /** @brief flag.XMLAttribute — 단독 토큰 `XMLAttribute` 또는 `XMLAttribute = true`. 같은 필드의 철자: XMLAttribute, xmlAttribute. */
        static constexpr AnnotationKey XMLAttribute{};
        /** @brief flag.XMLAttribute — 단독 토큰 `xmlAttribute` 또는 `xmlAttribute = true`. 같은 필드의 철자: XMLAttribute, xmlAttribute. */
        static constexpr AnnotationKey xmlAttribute{};
        /** @brief flag.AssetPath — 단독 토큰 `AssetPath` 또는 `AssetPath = true`. 같은 필드의 철자: AssetPath, assetPath. */
        static constexpr AnnotationKey AssetPath{};
        /** @brief flag.AssetPath — 단독 토큰 `assetPath` 또는 `assetPath = true`. 같은 필드의 철자: AssetPath, assetPath. */
        static constexpr AnnotationKey assetPath{};
        /** @brief flag.Polymorphic — 단독 토큰 `Polymorphic` 또는 `Polymorphic = true`. 같은 필드의 철자: Polymorphic, polymorphic. */
        static constexpr AnnotationKey Polymorphic{};
        /** @brief flag.Polymorphic — 단독 토큰 `polymorphic` 또는 `polymorphic = true`. 같은 필드의 철자: Polymorphic, polymorphic. */
        static constexpr AnnotationKey polymorphic{};
        /** @brief flag.Transient — 단독 토큰 `Transient` 또는 `Transient = true`. 같은 필드의 철자: Transient, transient, NonSerialized, nonSerialized. */
        static constexpr AnnotationKey Transient{};
        /** @brief flag.Transient — 단독 토큰 `transient` 또는 `transient = true`. 같은 필드의 철자: Transient, transient, NonSerialized, nonSerialized. */
        static constexpr AnnotationKey transient{};
        /** @brief flag.Transient — 단독 토큰 `NonSerialized` 또는 `NonSerialized = true`. 같은 필드의 철자: Transient, transient, NonSerialized, nonSerialized. */
        static constexpr AnnotationKey NonSerialized{};
        /** @brief flag.Transient — 단독 토큰 `nonSerialized` 또는 `nonSerialized = true`. 같은 필드의 철자: Transient, transient, NonSerialized, nonSerialized. */
        static constexpr AnnotationKey nonSerialized{};
        /** @brief flag.SkipIfEmpty — 단독 토큰 `SkipIfEmpty` 또는 `SkipIfEmpty = true`. 같은 필드의 철자: SkipIfEmpty, skipIfEmpty. 값이 비어 있으면 쓸 때 생략한다(읽기에는 영향 없음) */
        static constexpr AnnotationKey SkipIfEmpty{};
        /** @brief flag.SkipIfEmpty — 단독 토큰 `skipIfEmpty` 또는 `skipIfEmpty = true`. 같은 필드의 철자: SkipIfEmpty, skipIfEmpty. 값이 비어 있으면 쓸 때 생략한다(읽기에는 영향 없음) */
        static constexpr AnnotationKey skipIfEmpty{};
        /** @brief flag.HideInInspector — 단독 토큰 `HideInInspector` 또는 `HideInInspector = true`. 같은 필드의 철자: HideInInspector, hideInInspector, HiddenInInspector, EditorHidden. */
        static constexpr AnnotationKey HideInInspector{};
        /** @brief flag.HideInInspector — 단독 토큰 `hideInInspector` 또는 `hideInInspector = true`. 같은 필드의 철자: HideInInspector, hideInInspector, HiddenInInspector, EditorHidden. */
        static constexpr AnnotationKey hideInInspector{};
        /** @brief flag.HideInInspector — 단독 토큰 `HiddenInInspector` 또는 `HiddenInInspector = true`. 같은 필드의 철자: HideInInspector, hideInInspector, HiddenInInspector, EditorHidden. */
        static constexpr AnnotationKey HiddenInInspector{};
        /** @brief flag.HideInInspector — 단독 토큰 `EditorHidden` 또는 `EditorHidden = true`. 같은 필드의 철자: HideInInspector, hideInInspector, HiddenInInspector, EditorHidden. */
        static constexpr AnnotationKey EditorHidden{};
        /** @brief string.Alias — `Alias = "…"`. 여러 개면 Alias="hp, HitPoints" (따옴표가 필요하다. 그래야 콤마가 토큰 구분자로 읽히지 않는다) */
        static constexpr AnnotationKey Alias{};
        /** @brief string.Name — `Name = "…"`. 리플렉션 이름(직렬화 키)을 멤버 이름과 다르게 준다. 값이 객체 밖에 있는 프로퍼티(참조를 돌려주는 메서드에 붙인 PROPERTY)가 옛 필드 이름을 이어 쓸 때 쓴다 */
        static constexpr AnnotationKey Name{};
        /** @brief string.Category — `Category = "…"`. */
        static constexpr AnnotationKey Category{};
        /** @brief string.DisplayName — `DisplayName = "…"`. */
        static constexpr AnnotationKey DisplayName{};
        /** @brief string.Tooltip — `Tooltip = "…"`. */
        static constexpr AnnotationKey Tooltip{};
        /** @brief string.DefaultValue — `Default = "…"`. 같은 필드의 철자: Default, DefaultValue. */
        static constexpr AnnotationKey Default{};
        /** @brief string.DefaultValue — `DefaultValue = "…"`. 같은 필드의 철자: Default, DefaultValue. */
        static constexpr AnnotationKey DefaultValue{};
        /** @brief string.AssetType — `AssetType = "…"`. */
        static constexpr AnnotationKey AssetType{};
        /** @brief string.Meta — `Meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey Meta{};
        /** @brief string.Meta — `meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey meta{};
        /** @brief string.Meta — `CustomMeta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey CustomMeta{};
        /** @brief float.MinRange — `Min = 1.5`. */
        static constexpr AnnotationKey Min{};
        /** @brief float.MaxRange — `Max = 1.5`. */
        static constexpr AnnotationKey Max{};
        /** @brief float.UIMinRange — `UIMin = 1.5`. 인스펙터 슬라이더 · 드래그 범위(허용 범위 Min/Max 와 따로). 둘 다 적으면 슬라이더로 그리고, 값은 여전히 Min/Max 로 막는다 */
        static constexpr AnnotationKey UIMin{};
        /** @brief float.UIMaxRange — `UIMax = 1.5`. */
        static constexpr AnnotationKey UIMax{};
        /** @brief string.Units — `Units = "…"`. 저장된 값의 단위 — ReflectUnits.h 의 표(mm cm m km deg rad ms s min h percent ratio m/s km/h deg/s rad/s m/s2 Hz fps g kg N). 표에 없으면 파서 오류 */
        static constexpr AnnotationKey Units{};
        /** @brief string.EditCondition — `EditCondition = "…"`. 다른 프로퍼티가 참일 때만 고친다: "bEnabled" · "!bLocked" · "mode == Orbit" · "mode != Off". 거짓이면 인스펙터가 막는다(EditConditionHides 면 숨긴다) */
        static constexpr AnnotationKey EditCondition{};
        /** @brief flag.EditConditionHides — 단독 토큰 `EditConditionHides` 또는 `EditConditionHides = true`. */
        static constexpr AnnotationKey EditConditionHides{};
        /** @brief flag.ColorHdr — 단독 토큰 `ColorHdr` 또는 `ColorHdr = true`. float3/float4 색을 HDR(1 을 넘는 값)로 고친다. 색 선택기이기도 하다 */
        static constexpr AnnotationKey ColorHdr{};
        /** @brief flag.Multiline — 단독 토큰 `Multiline` 또는 `Multiline = true`. 여러 줄 글 칸(string) */
        static constexpr AnnotationKey Multiline{};
        /** @brief string.FileFilter — `FileFilter = "…"`. 경로 칸이 받는 파일: "*.png;*.dds". 끌어다 놓은 경로가 맞지 않으면 받지 않는다 */
        static constexpr AnnotationKey FileFilter{};
        /** @brief flag.Replicated — 단독 토큰 `Replicated` 또는 `Replicated = true`. 네트워크 복제 대상(`PropertyRoleUtil::collectReplicatedProperties`). RepNotify 를 적으면 Replicated 이기도 하다 */
        static constexpr AnnotationKey Replicated{};
        /** @brief string.RepNotify — `RepNotify = "…"`. 받은 값으로 바꾼 뒤 부를 같은 타입의 메서드 — void fn() 또는 void fn( const T& oldValue ). 없거나 모양이 다르면 파서 오류 */
        static constexpr AnnotationKey RepNotify{};
        /** @brief flag.SaveGame — 단독 토큰 `SaveGame` 또는 `SaveGame = true`. 타입(기반 포함)에 하나라도 있으면 세이브(`SerializeContext::setSaveGameOnly`)가 이것만 쓴다. 하나도 없는 타입은 전부 쓴다 */
        static constexpr AnnotationKey SaveGame{};
        /** @brief flag.Interp — 단독 토큰 `Interp` 또는 `Interp = true`. 시퀀서 값 트랙이 섞을 수 있다 — 실수 · 정수 · float2/3/4 · quaternion 만(그 밖의 타입은 파서 오류) */
        static constexpr AnnotationKey Interp{};
        /** @brief string.Validate — `Validate = "…"`. 프로퍼티 검증 함수 — 같은 타입의 void fn( ValidationContext& context ) [const]. 문제는 context.addError · addWarning 으로 적는다 */
        static constexpr AnnotationKey Validate{};
        /** @brief 단위 — `Units = mm` (Length). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey mm{};
        /** @brief 단위 — `Units = cm` (Length). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey cm{};
        /** @brief 단위 — `Units = m` (Length). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey m{};
        /** @brief 단위 — `Units = km` (Length). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey km{};
        /** @brief 단위 — `Units = deg` (Angle). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey deg{};
        /** @brief 단위 — `Units = rad` (Angle). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey rad{};
        /** @brief 단위 — `Units = ms` (Time). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey ms{};
        /** @brief 단위 — `Units = s` (Time). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey s{};
        /** @brief 단위 — `Units = min` (Time). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey min{};
        /** @brief 단위 — `Units = h` (Time). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey h{};
        /** @brief 단위 — `Units = percent` (Ratio). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey percent{};
        /** @brief 단위 — `Units = ratio` (Ratio). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey ratio{};
        /** @brief 단위 — `Units = Hz` (Frequency). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey Hz{};
        /** @brief 단위 — `Units = fps` (Frequency). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey fps{};
        /** @brief 단위 — `Units = g` (Mass). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey g{};
        /** @brief 단위 — `Units = kg` (Mass). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey kg{};
        /** @brief 단위 — `Units = N` (Force). 단위 표는 ReflectUnits.h 의 kArrReflectUnit 입니다. */
        static constexpr AnnotationKey N{};
    };

    inline constexpr const utf8* kArrPropertyKey[] = {
        "ReadOnly",
        "XMLAttribute",
        "xmlAttribute",
        "AssetPath",
        "assetPath",
        "Polymorphic",
        "polymorphic",
        "Transient",
        "transient",
        "NonSerialized",
        "nonSerialized",
        "SkipIfEmpty",
        "skipIfEmpty",
        "HideInInspector",
        "hideInInspector",
        "HiddenInInspector",
        "EditorHidden",
        "Alias",
        "Name",
        "Category",
        "DisplayName",
        "Tooltip",
        "Default",
        "DefaultValue",
        "AssetType",
        "Meta",
        "meta",
        "CustomMeta",
        "Min",
        "Max",
        "UIMin",
        "UIMax",
        "Units",
        "EditCondition",
        "EditConditionHides",
        "ColorHdr",
        "Multiline",
        "FileFilter",
        "Replicated",
        "RepNotify",
        "SaveGame",
        "Interp",
        "Validate",
        nullptr,
    };
} // namespace sw::reflect::attr

namespace sw::reflect::attr
{
    /** @brief `FUNCTION( … )` 의 키입니다. */
    struct FunctionKeys
    {
        /** @brief netrole.Server — 단독 토큰 `Server`(넷 역할). */
        static constexpr AnnotationKey Server{};
        /** @brief netrole.Client — 단독 토큰 `Client`(넷 역할). */
        static constexpr AnnotationKey Client{};
        /** @brief netrole.Multicast — 단독 토큰 `Multicast`(넷 역할). 같은 필드의 철자: Multicast, NetMulticast. */
        static constexpr AnnotationKey Multicast{};
        /** @brief netrole.Multicast — 단독 토큰 `NetMulticast`(넷 역할). 같은 필드의 철자: Multicast, NetMulticast. */
        static constexpr AnnotationKey NetMulticast{};
        /** @brief flag.Reliable — 단독 토큰 `Reliable` 또는 `Reliable = true`. */
        static constexpr AnnotationKey Reliable{};
        /** @brief flag.Validate — 단독 토큰 `Validate` 또는 `Validate = true`. */
        static constexpr AnnotationKey Validate{};
        /** @brief flag.CallInEditor — 단독 토큰 `CallInEditor` 또는 `CallInEditor = true`. 같은 필드의 철자: CallInEditor, callInEditor, ExecInEditor. */
        static constexpr AnnotationKey CallInEditor{};
        /** @brief flag.CallInEditor — 단독 토큰 `callInEditor` 또는 `callInEditor = true`. 같은 필드의 철자: CallInEditor, callInEditor, ExecInEditor. */
        static constexpr AnnotationKey callInEditor{};
        /** @brief flag.CallInEditor — 단독 토큰 `ExecInEditor` 또는 `ExecInEditor = true`. 같은 필드의 철자: CallInEditor, callInEditor, ExecInEditor. */
        static constexpr AnnotationKey ExecInEditor{};
        /** @brief string.EditorPreview — `EditorPreview = "…"`. 에디터 뷰포트 미리보기가 타입 이름 대신 이 값으로 메서드를 찾는다(EditorViewportPreview). 예: EditorPreview = "DialogueLine" */
        static constexpr AnnotationKey EditorPreview{};
        /** @brief string.Category — `Category = "…"`. */
        static constexpr AnnotationKey Category{};
        /** @brief string.DisplayName — `DisplayName = "…"`. */
        static constexpr AnnotationKey DisplayName{};
        /** @brief string.Tooltip — `Tooltip = "…"`. */
        static constexpr AnnotationKey Tooltip{};
        /** @brief string.Meta — `Meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey Meta{};
        /** @brief string.Meta — `meta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey meta{};
        /** @brief string.Meta — `CustomMeta = "…"`. 같은 필드의 철자: Meta, meta, CustomMeta. */
        static constexpr AnnotationKey CustomMeta{};
    };

    inline constexpr const utf8* kArrFunctionKey[] = {
        "Server",
        "Client",
        "Multicast",
        "NetMulticast",
        "Reliable",
        "Validate",
        "CallInEditor",
        "callInEditor",
        "ExecInEditor",
        "EditorPreview",
        "Category",
        "DisplayName",
        "Tooltip",
        "Meta",
        "meta",
        "CustomMeta",
        nullptr,
    };
} // namespace sw::reflect::attr

namespace sw::reflect::attr
{
    /** @brief `REFLECT_CONTAINER( 종류[, 래퍼] )` 의 종류입니다. 둘째 인자는 래퍼 이름(`List` → ListWrapper)입니다. */
    struct ContainerKeys
    {
        /** @brief 컨테이너 종류 `Sequence` */
        static constexpr AnnotationKey Sequence{};
        /** @brief 컨테이너 종류 `Map` */
        static constexpr AnnotationKey Map{};
    };
} // namespace sw::reflect::attr
