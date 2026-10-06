'use strict';

/**
 * @file LaunchArgumentUtil.js
 * @brief 고른 것(선택)과 명령줄 사이를 오가는 규칙입니다 — 값 검사 · 명령줄 만들기 · 명령줄 읽어 들이기 · 쓸 수 있는지 판정.
 * @details 명령줄 모양은 프로필 `command_line` 이 정합니다: 플래그는 `<prefix><key>`, 값은 `<prefix><key><separator><value>`.
 *          접두사가 `-` 로만 되어 있으면 읽을 때 `-` 를 몇 개든 뗍니다(`-key` · `--key` 를 같이 받는 흔한 파서와 같다).
 *          - 카탈로그에 없는 키라도 전역 변수 접두사(`name_prefix`)로 시작하면 전역 변수로 받습니다 — 늦게 등록되는 모듈 변수가 있기 때문입니다.
 *          - bool 은 `1 · 0 · true · false · yes · no · on · off`(대소문자 무시)를 받습니다.
 *          - 그 밖의 타입은 값이 반드시 있어야 하며, 읽지 못하는 값은 명령줄에 넣지 않고 알립니다.
 *          vscode 를 모르는 순수 모듈입니다(`Test/LaunchArgumentUtilTest.js`).
 */

const { ValueKind, kGeneralGroup } = require('./CatalogScanner');

/** @brief 항목 옆에 붙는 알림의 무게입니다. */
const NoteLevel = Object.freeze({
    Info: 'info',
    Warning: 'warning',
    Unavailable: 'unavailable',
});

const kTrueTextSet = new Set(['1', 'true', 'yes', 'on']);
const kFalseTextSet = new Set(['0', 'false', 'no', 'off']);
const kIntegerTextRe = /^[+-]?\d+$/;
const kFloatTextRe = /^[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?$/;
const kMinInt32 = -2147483648;
const kMaxInt32 = 2147483647;
/** @brief CMake Tools `shlex.quote` 가 따옴표 없이 두는 글자 밖의 글자입니다 — 하나라도 있으면 그 인자는 따옴표로 감싸집니다. */
const kShellQuotedCharRe = /[^\w@%\-+=:,./|><]/;
const kCmakeTrueTextSet = new Set(['ON', 'TRUE', '1', 'YES', 'Y']);

/** @brief 프로필이 없을 때의 명령줄 모양입니다. */
const kDefaultCommandLine = Object.freeze({ prefix: '-', separator: '=' });

/** @brief 빈 선택을 만듭니다. 맵의 값은 `{ bEnabled, value }`(인자는 `spelling` 도)입니다. */
function makeEmptySelection() {
    return {
        mapGlobalVariable: new Map(),
        mapArgument: new Map(),
        mapExclusiveChoice: new Map(),
        listCustomArgument: [],
        listEnvironment: [],
    };
}

/** @brief 선택을 깊이 복사합니다. 프리셋 저장 · 불러오기가 원본과 얽히지 않게 합니다. */
function cloneSelection(selection) {
    const clone = makeEmptySelection();
    for (const [name, state] of selection.mapGlobalVariable)
        clone.mapGlobalVariable.set(name, { ...state });
    for (const [name, state] of selection.mapArgument)
        clone.mapArgument.set(name, { ...state });
    for (const [groupId, argumentName] of selection.mapExclusiveChoice)
        clone.mapExclusiveChoice.set(groupId, argumentName);
    clone.listCustomArgument = selection.listCustomArgument.map((item) => ({ ...item }));
    clone.listEnvironment = selection.listEnvironment.map((item) => ({ ...item }));
    return clone;
}

/** @brief bool 글을 읽습니다. @return 'true' · 'false', 읽지 못하면 null. */
function normalizeBooleanText(text) {
    const lowered = String(text).trim().toLowerCase();
    if (kTrueTextSet.has(lowered))
        return 'true';
    if (kFalseTextSet.has(lowered))
        return 'false';
    return null;
}

/** @brief CMake 캐시 bool 글(`ON` · `TRUE` · `1` …)이 참인지 묻습니다. */
function isCmakeTrue(text) {
    return typeof text === 'string' && kCmakeTrueTextSet.has(text.trim().toUpperCase());
}

/**
 * @brief 값 글이 그 종류로 읽히는지 검사합니다.
 * @return `{ bValid, value, message }` — `value` 는 명령줄에 적을 정규화된 글, `message` 는 틀렸을 때의 설명입니다.
 */
function validateValue(valueKind, text, listEnumerator) {
    const rawText = text === undefined || text === null ? '' : String(text);
    const trimmed = rawText.trim();
    switch (valueKind) {
        case ValueKind.Boolean: {
            const booleanText = normalizeBooleanText(trimmed);
            if (booleanText === null)
                return { bValid: false, value: trimmed, message: 'true · false 중 하나여야 합니다' };
            return { bValid: true, value: booleanText, message: '' };
        }
        case ValueKind.Integer: {
            if (kIntegerTextRe.test(trimmed) === false)
                return { bValid: false, value: trimmed, message: '정수여야 합니다' };
            const parsed = Number(trimmed);
            if (parsed < kMinInt32 || kMaxInt32 < parsed)
                return { bValid: false, value: trimmed, message: 'int32 범위를 넘습니다' };
            return { bValid: true, value: String(parsed), message: '' };
        }
        case ValueKind.Float: {
            if (kFloatTextRe.test(trimmed) === false)
                return { bValid: false, value: trimmed, message: '실수여야 합니다' };
            return { bValid: true, value: trimmed, message: '' };
        }
        case ValueKind.Enum: {
            const bKnownEnumerator = Array.isArray(listEnumerator) && listEnumerator.includes(trimmed);
            if (bKnownEnumerator || kIntegerTextRe.test(trimmed))
                return { bValid: true, value: trimmed, message: '' };
            return { bValid: false, value: trimmed, message: '열거자 이름이나 정수여야 합니다' };
        }
        case ValueKind.String:
        default: {
            if (/[\r\n]/.test(rawText))
                return { bValid: false, value: rawText, message: '줄바꿈은 넣을 수 없습니다' };
            return { bValid: true, value: rawText, message: '' };
        }
    }
}

/**
 * @brief 명령줄 글 하나를 키와 값으로 나눕니다.
 * @return `{ key, value, bHasPrefix }` — 구분자가 없으면 `value` 는 null, 접두사가 없으면 `bHasPrefix` 는 false 입니다.
 */
function splitArgumentText(text, commandLine) {
    const trimmed = String(text).trim();
    const prefix = commandLine.prefix;
    let body = trimmed;
    let bHasPrefix = prefix === '';
    if (prefix !== '' && /^-+$/.test(prefix)) {
        const dashMatch = /^-*/.exec(trimmed);
        bHasPrefix = dashMatch[0].length > 0;
        body = trimmed.slice(dashMatch[0].length);
    } else if (prefix !== '' && trimmed.startsWith(prefix)) {
        bHasPrefix = true;
        body = trimmed.slice(prefix.length);
    }
    const separatorIndex = commandLine.separator === '' ? -1 : body.indexOf(commandLine.separator);
    return {
        key: separatorIndex < 0 ? body : body.slice(0, separatorIndex),
        value: separatorIndex < 0 ? null : body.slice(separatorIndex + commandLine.separator.length),
        bHasPrefix,
    };
}

/** @brief 플래그 글(`<prefix><key>`)입니다. */
function makeFlagText(key, commandLine) {
    return `${commandLine.prefix}${key}`;
}

/** @brief 값 글(`<prefix><key><separator><value>`)입니다. */
function makeValueText(key, value, commandLine) {
    return `${commandLine.prefix}${key}${commandLine.separator}${value}`;
}

/** @brief 카탈로그 인자 이름 → 항목, 철자 → 항목, 전역 변수 이름 집합을 만듭니다. */
function makeCatalogIndexInternal(catalog) {
    const mapNameToArgument = new Map();
    const mapSpellingToArgument = new Map();
    for (const argument of catalog.listArgument) {
        mapNameToArgument.set(argument.name, argument);
        for (const spelling of argument.listSpelling) {
            if (mapSpellingToArgument.has(spelling) === false)
                mapSpellingToArgument.set(spelling, argument);
        }
    }
    const uniqueVariableName = new Set(catalog.listGlobalVariable.map((variable) => variable.name));
    return { mapNameToArgument, mapSpellingToArgument, uniqueVariableName };
}

/** @brief 키가 전역 변수인지 묻습니다: 카탈로그에 있거나, 전역 변수 접두사로 시작합니다. */
function isGlobalVariableKeyInternal(key, catalogIndex, namePrefix) {
    return catalogIndex.uniqueVariableName.has(key) || (namePrefix !== '' && key.startsWith(namePrefix));
}

/**
 * @brief 전역 변수 이름으로 카탈로그 항목을 찾습니다. 같은 이름이 여러 변형(게임 · 플러그인)에 있으면 지금 빌드의 변형을, 없으면 첫 것을 씁니다.
 * @param mapCacheValue 빌드 캐시 값(이름 → 글)입니다. 모르면 빈 객체입니다.
 * @return 항목, 없으면 null 입니다.
 */
function findGlobalVariable(catalog, name, mapCacheValue) {
    let firstMatch = null;
    for (const variable of catalog.listGlobalVariable) {
        if (variable.name !== name)
            continue;
        if (variable.variantName === '' || mapCacheValue[variable.variantCacheVariable] === variable.variantName)
            return variable;
        if (firstMatch === null)
            firstMatch = variable;
    }
    return firstMatch;
}

/**
 * @brief 항목을 처음 켤 때 넣을 값입니다. bool 은 기본값의 반대(켠다는 것은 바꾸고 싶다는 뜻), 나머지는 기본값입니다.
 */
function makeInitialValue(entry) {
    if (entry.valueKind === ValueKind.Boolean)
        return normalizeBooleanText(entry.defaultText) === 'true' ? 'false' : 'true';
    const bEnumWithoutDefault = entry.valueKind === ValueKind.Enum && Array.isArray(entry.listEnumerator) && entry.listEnumerator.includes(entry.defaultText) === false;
    if (bEnumWithoutDefault && entry.listEnumerator.length > 0)
        return entry.listEnumerator[0];
    return entry.defaultText;
}

/**
 * @brief 선택을 명령줄로 만듭니다. 순서: 배타 묶음(묶음 순서) → 인자(파일 순서) → 전역 변수(이름 순) → 사용자 인자(적은 순서).
 * @param options `{ commandLine, mapCacheValue }` 입니다.
 * @details 값이 틀린 항목은 넣지 않고 `listIssue` 로 알립니다 — 넣어 봐야 프로그램이 경고와 함께 버리거나 잘못 읽습니다.
 * @return `{ listArgument, listIssue }` — `listIssue` 는 `[{ key, message }]` 이고 `key` 는 화면 항목의 키(`gv:…` · `arg:…` · `group:…` · `custom:n`)입니다.
 */
function composeCommandLine(catalog, selection, options) {
    const commandLine = options.commandLine;
    const listArgument = [];
    const listIssue = [];
    const catalogIndex = makeCatalogIndexInternal(catalog);

    const listGroupId = catalog.listExclusiveGroup.map((group) => group.id);
    for (const groupId of selection.mapExclusiveChoice.keys()) {
        if (listGroupId.includes(groupId) === false)
            listGroupId.push(groupId);
    }
    for (const groupId of listGroupId) {
        const argumentName = selection.mapExclusiveChoice.get(groupId);
        if (argumentName === undefined || argumentName === '')
            continue;
        const argument = catalogIndex.mapNameToArgument.get(argumentName);
        if (argument === undefined || argument.group !== groupId)
            listIssue.push({ key: `group:${groupId}`, message: `${argumentName} 는 ${groupId} 묶음의 인자가 아닙니다` });
        else
            listArgument.push(makeFlagText(argument.listSpelling[0], commandLine));
    }

    const listArgumentName = [];
    for (const argument of catalog.listArgument) {
        if (argument.group === kGeneralGroup)
            listArgumentName.push(argument.name);
    }
    const listUnknownArgumentName = Array.from(selection.mapArgument.keys()).filter((name) => catalogIndex.mapNameToArgument.has(name) === false).sort();
    for (const name of listArgumentName.concat(listUnknownArgumentName)) {
        const state = selection.mapArgument.get(name);
        if (state === undefined || state.bEnabled === false)
            continue;
        const argument = catalogIndex.mapNameToArgument.get(name);
        const spelling = argument === undefined ? state.spelling : argument.listSpelling[0];
        if (spelling === undefined || spelling === '') {
            listIssue.push({ key: `arg:${name}`, message: `인자 ${name} 는 카탈로그에 없어 철자를 모릅니다` });
            continue;
        }
        const valueKind = argument === undefined ? ValueKind.String : argument.valueKind;
        if (valueKind === ValueKind.Boolean || (argument === undefined && (state.value === null || state.value === ''))) {
            listArgument.push(makeFlagText(spelling, commandLine));
            continue;
        }
        const checked = validateValue(valueKind, state.value, []);
        if (checked.bValid === false) {
            listIssue.push({ key: `arg:${name}`, message: `${makeFlagText(spelling, commandLine)}: ${checked.message}` });
            continue;
        }
        listArgument.push(makeValueText(spelling, checked.value, commandLine));
    }

    for (const name of Array.from(selection.mapGlobalVariable.keys()).sort()) {
        const state = selection.mapGlobalVariable.get(name);
        if (state.bEnabled === false)
            continue;
        const variable = findGlobalVariable(catalog, name, options.mapCacheValue);
        const valueKind = variable === null ? ValueKind.String : variable.valueKind;
        const checked = validateValue(valueKind, state.value, variable === null ? [] : variable.listEnumerator);
        if (checked.bValid === false) {
            listIssue.push({ key: `gv:${name}`, message: `${makeFlagText(name, commandLine)}: ${checked.message}` });
            continue;
        }
        listArgument.push(makeValueText(name, checked.value, commandLine));
    }

    for (let index = 0; index < selection.listCustomArgument.length; index += 1) {
        const item = selection.listCustomArgument[index];
        const text = item.text.trim();
        if (item.bEnabled && text !== '')
            listArgument.push(text);
        else if (item.bEnabled && text === '')
            listIssue.push({ key: `custom:${index}`, message: '빈 사용자 인자는 넣지 않습니다' });
    }
    return { listArgument, listIssue };
}

/** @brief 선택의 환경 변수 중 켜진 것을 CMake Tools `debugConfig.environment` 모양(`[{ name, value }]`)으로 만듭니다. */
function composeEnvironment(selection) {
    const listEnvironment = [];
    for (const item of selection.listEnvironment) {
        const name = item.name.trim();
        if (item.bEnabled && name !== '')
            listEnvironment.push({ name, value: item.value });
    }
    return listEnvironment;
}

/**
 * @brief 명령줄 글 하나를 선택의 제 칸에 넣습니다. 카탈로그가 모르는 글이면 false 입니다(부르는 쪽이 사용자 인자로 둔다).
 */
function applyArgumentTextInternal(selection, text, catalogIndex, options) {
    const parsed = splitArgumentText(text, options.commandLine);
    if (parsed.bHasPrefix === false || parsed.key === '')
        return false;
    if (isGlobalVariableKeyInternal(parsed.key, catalogIndex, options.namePrefix)) {
        selection.mapGlobalVariable.set(parsed.key, { bEnabled: true, value: parsed.value === null ? 'true' : parsed.value });
        return true;
    }
    const argument = catalogIndex.mapSpellingToArgument.get(parsed.key);
    if (argument === undefined)
        return false;
    if (argument.group !== kGeneralGroup) {
        selection.mapExclusiveChoice.set(argument.group, argument.name);
        return true;
    }
    const previous = selection.mapArgument.get(argument.name);
    let value = parsed.value;
    if (value === null)
        value = argument.valueKind === ValueKind.Boolean ? 'true' : (previous === undefined ? argument.defaultText : previous.value);
    selection.mapArgument.set(argument.name, { bEnabled: true, value, spelling: parsed.key });
    return true;
}

/**
 * @brief 남이 쓴 명령줄(설정 파일을 손으로 고침 · git pull)을 선택으로 읽어 들입니다.
 * @details 명령줄이 "무엇이 켜졌는가" 의 정본입니다. 이전 선택은 꺼진 항목의 값 · 꺼진 사용자 인자를 기억하는 데만 씁니다.
 *          카탈로그가 모르는 글은 적힌 그대로 사용자 인자로 둡니다(잃어버리지 않는다).
 * @param options `{ commandLine, namePrefix }` 입니다.
 * @return 새 선택입니다. @p previousSelection 은 바꾸지 않습니다.
 */
function importCommandLine(listText, listEnvironmentSetting, catalog, previousSelection, options) {
    const selection = cloneSelection(previousSelection);
    const catalogIndex = makeCatalogIndexInternal(catalog);
    for (const state of selection.mapGlobalVariable.values())
        state.bEnabled = false;
    for (const state of selection.mapArgument.values())
        state.bEnabled = false;
    selection.mapExclusiveChoice.clear();
    const listDisabledCustom = selection.listCustomArgument.filter((item) => item.bEnabled === false);
    selection.listCustomArgument = [];

    for (const rawText of listText) {
        const text = String(rawText);
        if (applyArgumentTextInternal(selection, text, catalogIndex, options) === false)
            selection.listCustomArgument.push({ text, bEnabled: true });
    }
    for (const item of listDisabledCustom) {
        if (selection.listCustomArgument.some((listed) => listed.text === item.text) === false)
            selection.listCustomArgument.push(item);
    }

    if (Array.isArray(listEnvironmentSetting)) {
        const listDisabledEnvironment = selection.listEnvironment.filter((item) => item.bEnabled === false);
        selection.listEnvironment = [];
        for (const item of listEnvironmentSetting) {
            if (item !== null && typeof item === 'object' && typeof item.name === 'string')
                selection.listEnvironment.push({ name: item.name, value: item.value === undefined ? '' : String(item.value), bEnabled: true });
        }
        for (const item of listDisabledEnvironment) {
            if (selection.listEnvironment.some((listed) => listed.name === item.name) === false)
                selection.listEnvironment.push(item);
        }
    }
    return selection;
}

/**
 * @brief 켜진 사용자 인자 중 이제 카탈로그가 아는 것을 제 칸으로 옮깁니다(붙여 넣기 · 스캔이 늦게 끝남 · 소스에 새 인자).
 * @param options `{ commandLine, namePrefix }` 입니다.
 * @return 하나라도 옮겼으면 true 입니다.
 */
function adoptKnownCustomArguments(selection, catalog, options) {
    const catalogIndex = makeCatalogIndexInternal(catalog);
    const listRemaining = [];
    let bChanged = false;
    for (const item of selection.listCustomArgument) {
        if (item.bEnabled && applyArgumentTextInternal(selection, item.text, catalogIndex, options))
            bChanged = true;
        else
            listRemaining.push(item);
    }
    selection.listCustomArgument = listRemaining;
    return bChanged;
}

/** @brief 선택에서 철자 @p spelling 인 인자(일반 · 배타 묶음)가 켜져 있는지 묻습니다. */
function isArgumentEnabled(selection, catalog, spelling) {
    const argument = catalog.listArgument.find((item) => item.listSpelling.includes(spelling));
    if (argument === undefined)
        return false;
    if (argument.group !== kGeneralGroup)
        return selection.mapExclusiveChoice.get(argument.group) === argument.name;
    const state = selection.mapArgument.get(argument.name);
    return state !== undefined && state.bEnabled;
}

/** @brief 규칙의 `when`(빌드 캐시 조건)이 지금 맞는지 묻습니다. 문맥을 모르면 조건 있는 규칙은 맞지 않습니다. */
function isRuleConditionMetInternal(when, context) {
    if (when === null)
        return true;
    if (context.bKnown === false || (when.cacheVariable in context.mapCacheValue) === false)
        return false;
    const value = context.mapCacheValue[when.cacheVariable];
    if (when.equals !== null)
        return value === when.equals;
    return isCmakeTrue(value) === when.bTrue;
}

/**
 * @brief 지금 빌드 문맥에서 전역 변수 하나를 쓸 수 있는지 판정합니다.
 * @param context `{ bKnown, mapCacheValue }` 입니다.
 * @return `{ level, text, bHidden }` 또는 알릴 것이 없으면 null 입니다. `bHidden` 은 다른 변형(게임 · 플러그인)의 변수라 숨길 것인지입니다.
 */
function computeGlobalVariableNote(variable, profile, context, selection, catalog) {
    if (context.bKnown && variable.variantName !== '') {
        const activeVariant = context.mapCacheValue[variable.variantCacheVariable];
        if (typeof activeVariant === 'string' && activeVariant !== '' && activeVariant !== variable.variantName)
            return { level: NoteLevel.Unavailable, text: `${variable.variantName} 의 변수 — 지금 빌드(${variable.variantCacheVariable}=${activeVariant})에는 없습니다`, bHidden: true };
    }
    for (const rule of profile.listRule) {
        const bTagMatches = rule.match.tag === '' || rule.match.tag === variable.tag;
        const bModuleMatches = rule.match.module === '' || rule.match.module === variable.moduleName;
        if (bTagMatches === false || bModuleMatches === false || isRuleConditionMetInternal(rule.when, context) === false)
            continue;
        if (rule.unlessArgument !== '' && isArgumentEnabled(selection, catalog, rule.unlessArgument))
            continue;
        return { level: rule.level, text: rule.message, bHidden: false };
    }
    return null;
}

/**
 * @brief PowerShell 5.1 이 쪼개는 인자가 있는지 묻습니다.
 * @details Windows PowerShell 은 `-` 로 시작하고 `.` 가 든 네이티브 인자를 점 앞에서 둘로 나눕니다
 *          (`-x=out.ppm` → `-x=out` `.ppm`). CMake Tools 의 ▷ 실행은 인자를 따옴표 없이 터미널에 보내므로 기본 터미널이
 *          PowerShell 이면 이 인자가 깨집니다. 이 확장의 실행 · 디버그는 셸을 거치지 않아 안전합니다.
 *          CMake Tools 의 `shlex.quote` 가 따옴표로 감싸는 글(`kShellQuotedCharRe`)은 쪼개지지 않으므로 세지 않습니다.
 */
function hasPowerShellSplitArgument(listArgument) {
    return listArgument.some((text) => text.startsWith('-') && text.includes('.') && kShellQuotedCharRe.test(text) === false);
}

/**
 * @brief 붙여 넣은 명령줄 글을 인자 하나씩으로 나눕니다. 공백으로 나누되 큰따옴표 안의 공백은 나누지 않고 따옴표는 뗍니다.
 */
function splitCommandLineText(text) {
    const listArgument = [];
    let current = '';
    let bInQuote = false;
    let bHasToken = false;
    for (const character of String(text)) {
        if (character === '"') {
            bInQuote = bInQuote === false;
            bHasToken = true;
            continue;
        }
        if (bInQuote === false && /\s/.test(character)) {
            if (bHasToken)
                listArgument.push(current);
            current = '';
            bHasToken = false;
            continue;
        }
        current += character;
        bHasToken = true;
    }
    if (bHasToken)
        listArgument.push(current);
    return listArgument;
}

/**
 * @brief 명령줄을 PowerShell · cmd 에 그대로 붙여 넣을 수 있는 한 줄로 만듭니다.
 * @details 공백 · 따옴표 · 특수 글자가 있거나, PowerShell 이 쪼개는 모양(`-…` + `.`)이면 큰따옴표로 감쌉니다.
 */
function makeShellCommandLine(listArgument) {
    const listQuoted = [];
    for (const text of listArgument) {
        const bNeedsQuote = text === '' || kShellQuotedCharRe.test(text) || (text.startsWith('-') && text.includes('.'));
        listQuoted.push(bNeedsQuote ? `"${text.replace(/"/g, '\\"')}"` : text);
    }
    return listQuoted.join(' ');
}

/** @brief 디버그 구성(launch.json)에 패널의 인자를 넣는 방식입니다 — 구성의 `launchArgs` 칸 값입니다. */
const LaunchInjectMode = Object.freeze({
    Append: 'append',
    Replace: 'replace',
});

/** @brief 디버그 구성에서 주입 방식을 적는 칸 이름입니다. 디버거에 넘기기 전에 지웁니다. */
const kLaunchInjectKey = 'launchArgs';

/**
 * @brief 디버거 종류 → 환경 변수 칸 모양입니다. 표에 없는 디버거(CodeLLDB `lldb` · node · python …)는 `env` 객체를 씁니다.
 * @details MS C++ 디버거(cppdbg · cppvsdbg)만 `environment: [{ name, value }]` 배열을 읽고, CMake Tools 도 그 모양으로 넘깁니다.
 */
const kMapDebuggerEnvironmentStyle = new Map([
    ['cppdbg', 'array'],
    ['cppvsdbg', 'array'],
]);

/** @brief 디버거 종류의 환경 변수 칸 모양(`array` · `object`)입니다. */
function getDebuggerEnvironmentStyle(debuggerType) {
    return kMapDebuggerEnvironmentStyle.has(debuggerType) ? kMapDebuggerEnvironmentStyle.get(debuggerType) : 'object';
}

/**
 * @brief 디버그 구성 하나를 디버거가 받기 직전에 고칩니다.
 * @details 둘을 합니다.
 *          1. 환경 변수 모양 맞추기 — `env` 객체를 읽는 디버거(CodeLLDB 등)인데 `environment` 배열이 있으면 `env` 로 옮깁니다.
 *             CMake Tools 디버그(`cmake.debugConfig.type: "lldb"`)가 환경 변수를 cppdbg 모양으로 넘기기 때문입니다. 이미 있는 `env` 키가 이깁니다.
 *          2. 주입 — 구성에 `"launchArgs": "append"`(기존 `args` 뒤에) · `"replace"`(기존 `args` 대신)가 있으면 패널의 인자 · 환경 변수를 넣습니다.
 *             그 칸이 없는 구성은 인자를 건드리지 않습니다(손으로 적은 구성을 몰래 바꾸지 않는다).
 * @return `{ config, bInjected, problem }` — `config` 는 새 객체이고 @p config 는 바꾸지 않습니다. `problem` 은 모르는 주입 방식 등입니다.
 */
function applyLaunchArguments(config, listArgument, listEnvironment) {
    const result = { config: { ...config }, bInjected: false, problem: '' };
    const nextConfig = result.config;
    const environmentStyle = getDebuggerEnvironmentStyle(String(nextConfig.type));
    if (environmentStyle === 'object' && Array.isArray(nextConfig.environment)) {
        const environmentFromArray = {};
        for (const item of nextConfig.environment) {
            if (item !== null && typeof item === 'object' && typeof item.name === 'string')
                environmentFromArray[item.name] = item.value === undefined ? '' : String(item.value);
        }
        const existingEnvironment = nextConfig.env !== null && typeof nextConfig.env === 'object' ? nextConfig.env : {};
        nextConfig.env = { ...environmentFromArray, ...existingEnvironment };
        delete nextConfig.environment;
    }

    if ((kLaunchInjectKey in nextConfig) === false)
        return result;
    const mode = nextConfig[kLaunchInjectKey];
    delete nextConfig[kLaunchInjectKey];
    if (mode !== LaunchInjectMode.Append && mode !== LaunchInjectMode.Replace) {
        result.problem = `"${kLaunchInjectKey}": "${mode}" is not "append" or "replace" - arguments were not injected`;
        return result;
    }

    let listExistingArgument = [];
    if (Array.isArray(nextConfig.args))
        listExistingArgument = nextConfig.args.map(String);
    else if (typeof nextConfig.args === 'string')
        listExistingArgument = splitCommandLineText(nextConfig.args);
    nextConfig.args = mode === LaunchInjectMode.Replace ? listArgument.slice() : listExistingArgument.concat(listArgument);

    if (environmentStyle === 'array') {
        const uniqueInjectedName = new Set(listEnvironment.map((item) => item.name));
        const listExistingEnvironment = Array.isArray(nextConfig.environment) ? nextConfig.environment.filter((item) => uniqueInjectedName.has(item.name) === false) : [];
        nextConfig.environment = listExistingEnvironment.concat(listEnvironment.map((item) => ({ name: item.name, value: item.value })));
    } else if (listEnvironment.length > 0) {
        const existingEnvironment = nextConfig.env !== null && typeof nextConfig.env === 'object' ? nextConfig.env : {};
        nextConfig.env = { ...existingEnvironment };
        for (const item of listEnvironment)
            nextConfig.env[item.name] = item.value;
    }
    result.bInjected = true;
    return result;
}

module.exports = {
    LaunchInjectMode,
    kLaunchInjectKey,
    getDebuggerEnvironmentStyle,
    applyLaunchArguments,
    NoteLevel,
    kDefaultCommandLine,
    makeEmptySelection,
    cloneSelection,
    normalizeBooleanText,
    isCmakeTrue,
    validateValue,
    splitArgumentText,
    makeFlagText,
    makeValueText,
    findGlobalVariable,
    makeInitialValue,
    composeCommandLine,
    composeEnvironment,
    importCommandLine,
    adoptKnownCustomArguments,
    isArgumentEnabled,
    computeGlobalVariableNote,
    hasPowerShellSplitArgument,
    splitCommandLineText,
    makeShellCommandLine,
};
