'use strict';

/**
 * @file CatalogProfile.js
 * @brief "소스의 무엇이 넘길 수 있는 것인가" 를 적은 프로필을 읽고 기본값으로 채웁니다. 확장 코드는 특정 프로젝트를 모릅니다.
 * @details 프로필은 설정 `launchArgs.catalog` 에 객체로 적거나, 작업 폴더 기준 JSON 파일 경로로 적습니다
 *          (예: `Tools/launch-args/Profiles/SwEngine.json`). 프로필이 없으면 카탈로그가 비고, 사용자 인자 · 환경 변수 ·
 *          프리셋 · 실행 · 디버그만 됩니다(어느 CMake 프로젝트에서나).
 *          JSON 키는 snake_case 입니다. 칸의 뜻은 `README.md` "프로필" 절과 `Profiles/SwEngine.json` 이 보여 줍니다.
 *          vscode 를 모르는 순수 모듈입니다.
 */

const fs = require('fs');
const path = require('path');

/** @brief 값 종류 이름입니다. 프로필 `type_map` 의 값이 이 중 하나입니다. */
const kValueKindSet = new Set(['bool', 'int', 'float', 'string', 'enum']);
/** @brief 태그가 없는 매크로(일반 변수)의 기본 표시 이름입니다. 프로필 `macros[].label` 이 덧씁니다. */
const kUntaggedLabel = '일반';
/** @brief 알림 무게입니다. */
const kRuleLevelSet = new Set(['info', 'warning', 'unavailable']);

/** @brief 기본 타입 표입니다(C++ 흔한 타입). 프로필 `type_map` 이 덧씁니다. */
const kDefaultTypeMap = Object.freeze({
    bool: 'bool',
    int: 'int',
    int32: 'int',
    int32_t: 'int',
    int64: 'int',
    int64_t: 'int',
    uint32: 'int',
    uint32_t: 'int',
    unsigned: 'int',
    float: 'float',
    float32: 'float',
    double: 'float',
    float64: 'float',
    string: 'string',
    'std::string': 'string',
    'const char*': 'string',
});

/** @brief 객체인지(배열 · null 제외) 묻습니다. */
function isPlainObjectInternal(value) {
    return value !== null && typeof value === 'object' && Array.isArray(value) === false;
}

/** @brief 글 배열을 읽습니다. 글 하나면 배열 하나로 받습니다. */
function readStringListInternal(value) {
    if (typeof value === 'string')
        return value === '' ? [] : [value];
    if (Array.isArray(value) === false)
        return [];
    return value.filter((item) => typeof item === 'string' && item !== '');
}

/** @brief 0 이상의 정수 칸을 읽습니다. 없거나 틀리면 @p fallback 입니다. */
function readIndexInternal(value, fallback) {
    return Number.isInteger(value) && 0 <= value ? value : fallback;
}

/** @brief 글 칸을 읽습니다. */
function readStringInternal(value, fallback) {
    return typeof value === 'string' ? value : fallback;
}

/** @brief 빈 프로필입니다 — 카탈로그 없이 사용자 인자만 씁니다. */
function makeEmptyProfile() {
    return normalizeProfile({});
}

/**
 * @brief 프로필 JSON 을 읽어 모든 칸이 있는 객체로 만듭니다. 틀린 칸은 버리고 `listProblem` 에 남깁니다.
 */
function normalizeProfile(raw) {
    const json = isPlainObjectInternal(raw) ? raw : {};
    const listProblem = [];

    const globalJson = isPlainObjectInternal(json.global_variable) ? json.global_variable : {};
    const globalIndexJson = isPlainObjectInternal(globalJson.argument_index) ? globalJson.argument_index : {};
    const listGlobalMacro = [];
    for (const item of Array.isArray(globalJson.macros) ? globalJson.macros : []) {
        if (typeof item === 'string') {
            listGlobalMacro.push({ name: item, tag: '', label: kUntaggedLabel });
        } else if (isPlainObjectInternal(item) && typeof item.name === 'string') {
            const tag = readStringInternal(item.tag, '');
            listGlobalMacro.push({ name: item.name, tag, label: readStringInternal(item.label, tag === '' ? kUntaggedLabel : tag) });
        }
        else
            listProblem.push('global_variable.macros: each item is a macro name or { name, tag }');
    }
    const mapType = {};
    for (const [typeName, valueKind] of Object.entries(kDefaultTypeMap))
        mapType[typeName.replace(/\s+/g, '')] = valueKind;
    if (isPlainObjectInternal(globalJson.type_map)) {
        for (const [typeName, valueKind] of Object.entries(globalJson.type_map)) {
            if (kValueKindSet.has(valueKind))
                mapType[typeName.replace(/\s+/g, '')] = valueKind;
            else
                listProblem.push(`global_variable.type_map.${typeName}: "${valueKind}" is not one of ${Array.from(kValueKindSet).join(', ')}`);
        }
    }
    const mapDefaultValueMacro = {};
    if (isPlainObjectInternal(globalJson.default_value_macros)) {
        for (const [macroName, source] of Object.entries(globalJson.default_value_macros)) {
            if (typeof source === 'string')
                mapDefaultValueMacro[macroName] = { value: source, file: '', key: '' };
            else if (isPlainObjectInternal(source) && typeof source.file === 'string' && typeof source.key === 'string')
                mapDefaultValueMacro[macroName] = { value: '', file: source.file, key: source.key };
            else
                listProblem.push(`global_variable.default_value_macros.${macroName}: a value or { file, key }`);
        }
    }

    const argumentJson = isPlainObjectInternal(json.argument) ? json.argument : {};
    const argumentIndexJson = isPlainObjectInternal(argumentJson.argument_index) ? argumentJson.argument_index : {};
    const listExclusiveGroup = [];
    for (const item of Array.isArray(argumentJson.exclusive_groups) ? argumentJson.exclusive_groups : []) {
        if (isPlainObjectInternal(item) === false || typeof item.id !== 'string' || item.id === '') {
            listProblem.push('argument.exclusive_groups: each group needs an "id"');
            continue;
        }
        listExclusiveGroup.push({
            id: item.id,
            label: readStringInternal(item.label, item.id),
            listArgumentName: readStringListInternal(item.arguments),
            tableMacro: readStringInternal(item.table_macro, ''),
            file: readStringInternal(item.file, ''),
            rowsKey: readStringInternal(item.rows_key, ''),
            nameKey: readStringInternal(item.name_key, 'name'),
            labelKey: readStringInternal(item.label_key, ''),
            spellingsKey: readStringInternal(item.spellings_key, 'spellings'),
            defaultKey: readStringInternal(item.default_key, ''),
        });
    }

    const commandLineJson = isPlainObjectInternal(json.command_line) ? json.command_line : {};
    const moduleJson = isPlainObjectInternal(json.module) ? json.module : {};
    const listVariantFolder = [];
    for (const item of Array.isArray(moduleJson.variant_folders) ? moduleJson.variant_folders : []) {
        if (isPlainObjectInternal(item) && typeof item.folder === 'string' && typeof item.cache_variable === 'string')
            listVariantFolder.push({ folder: item.folder, cacheVariable: item.cache_variable, label: readStringInternal(item.label, item.folder) });
        else
            listProblem.push('module.variant_folders: each item is { folder, cache_variable }');
    }

    const listRule = [];
    for (const item of Array.isArray(json.rules) ? json.rules : []) {
        if (isPlainObjectInternal(item) === false || kRuleLevelSet.has(item.level) === false || typeof item.message !== 'string') {
            listProblem.push(`rules: each rule needs "level" (${Array.from(kRuleLevelSet).join(', ')}) and "message"`);
            continue;
        }
        const whenJson = isPlainObjectInternal(item.when) ? item.when : null;
        const matchJson = isPlainObjectInternal(item.match) ? item.match : {};
        listRule.push({
            when: whenJson === null ? null : { cacheVariable: readStringInternal(whenJson.cache_variable, ''), bTrue: whenJson.is_true !== false, equals: typeof whenJson.equals === 'string' ? whenJson.equals : null },
            match: { tag: readStringInternal(matchJson.tag, ''), module: readStringInternal(matchJson.module, '') },
            unlessArgument: readStringInternal(item.unless_argument, ''),
            level: item.level,
            message: item.message,
        });
    }

    const listStatus = [];
    for (const item of Array.isArray(json.status) ? json.status : []) {
        if (isPlainObjectInternal(item) && typeof item.cache_variable === 'string')
            listStatus.push({ cacheVariable: item.cache_variable, label: readStringInternal(item.label, ''), whenTrue: readStringInternal(item.when_true, ''), whenFalse: readStringInternal(item.when_false, '') });
    }

    return {
        name: readStringInternal(json.name, ''),
        globalVariable: {
            listFileGlob: readStringListInternal(globalJson.files),
            listMacro: listGlobalMacro,
            index: {
                type: readIndexInternal(globalIndexJson.type, 0),
                name: readIndexInternal(globalIndexJson.name, 1),
                defaultValue: readIndexInternal(globalIndexJson.default_value, 2),
                description: readIndexInternal(globalIndexJson.description, 3),
            },
            namePrefix: readStringInternal(globalJson.name_prefix, ''),
            mapType,
            mapDefaultValueMacro,
        },
        argument: {
            listFileGlob: readStringListInternal(argumentJson.files),
            macroName: readStringInternal(argumentJson.macro, ''),
            index: {
                name: readIndexInternal(argumentIndexJson.name, 0),
                defaultValue: readIndexInternal(argumentIndexJson.default_value, 1),
                spellingStart: readIndexInternal(argumentIndexJson.spelling_start, 2),
            },
            maxCommentLine: readIndexInternal(argumentJson.max_comment_lines, 3),
            listExclusiveGroup,
        },
        commandLine: {
            prefix: readStringInternal(commandLineJson.prefix, '-'),
            separator: readStringInternal(commandLineJson.separator, '='),
        },
        module: {
            root: readStringInternal(moduleJson.root, '').replace(/\/+$/, ''),
            mapLabel: isPlainObjectInternal(moduleJson.labels) ? { ...moduleJson.labels } : {},
            listVariantFolder,
        },
        listRule,
        listStatus,
        listProblem,
    };
}

/**
 * @brief 설정값(객체 · 파일 경로 · 없음)에서 프로필을 읽습니다.
 * @return `{ profile, sourceText, problem }` — `sourceText` 는 화면에 보일 출처, `problem` 은 읽지 못한 이유입니다.
 */
function loadProfile(settingValue, workspaceRoot) {
    if (settingValue === undefined || settingValue === null || settingValue === '')
        return { profile: makeEmptyProfile(), sourceText: '', problem: '' };
    if (isPlainObjectInternal(settingValue))
        return { profile: normalizeProfile(settingValue), sourceText: 'settings', problem: '' };
    if (typeof settingValue !== 'string')
        return { profile: makeEmptyProfile(), sourceText: '', problem: 'launchArgs.catalog must be an object or a JSON file path' };
    const profilePath = path.isAbsolute(settingValue) ? settingValue : path.join(workspaceRoot, settingValue);
    try {
        return { profile: normalizeProfile(JSON.parse(fs.readFileSync(profilePath, 'utf8'))), sourceText: settingValue, problem: '' };
    } catch (error) {
        return { profile: makeEmptyProfile(), sourceText: settingValue, problem: `cannot read profile ${settingValue}: ${error.message}` };
    }
}

/** @brief 프로필이 카탈로그를 만들 것이 있는지 묻습니다. */
function hasCatalogSource(profile) {
    const bGlobal = profile.globalVariable.listFileGlob.length > 0 && profile.globalVariable.listMacro.length > 0;
    const bArgument = profile.argument.listFileGlob.length > 0 && profile.argument.macroName !== '';
    return bGlobal || bArgument;
}

/** @brief 프로필이 읽는 CMake 캐시 변수 이름 전부입니다(빌드 문맥이 이것만 읽어 온다). */
function collectCacheVariableNames(profile) {
    const uniqueName = new Set();
    for (const variant of profile.module.listVariantFolder)
        uniqueName.add(variant.cacheVariable);
    for (const rule of profile.listRule) {
        if (rule.when !== null && rule.when.cacheVariable !== '')
            uniqueName.add(rule.when.cacheVariable);
    }
    for (const status of profile.listStatus)
        uniqueName.add(status.cacheVariable);
    return Array.from(uniqueName).sort();
}

/** @brief 정규식 특수 글자를 이스케이프합니다. */
function escapeRegExpInternal(text) {
    return text.replace(/[.+^$()|[\]\\]/g, '\\$&');
}

/**
 * @brief glob(`**` · `*` · `?` · `{a,b}`)을 저장소 기준 `/` 경로에 맞추는 정규식으로 바꿉니다.
 */
function makeGlobRegExp(glob) {
    let pattern = '';
    let braceDepth = 0;
    for (let index = 0; index < glob.length; index += 1) {
        const character = glob[index];
        if (character === '*' && glob[index + 1] === '*') {
            const bFollowedBySlash = glob[index + 2] === '/';
            pattern += bFollowedBySlash ? '(?:.*/)?' : '.*';
            index += bFollowedBySlash ? 2 : 1;
        } else if (character === '*') {
            pattern += '[^/]*';
        } else if (character === '?') {
            pattern += '[^/]';
        } else if (character === '{') {
            braceDepth += 1;
            pattern += '(?:';
        } else if (character === '}' && 0 < braceDepth) {
            braceDepth -= 1;
            pattern += ')';
        } else if (character === ',' && 0 < braceDepth) {
            pattern += '|';
        } else {
            pattern += escapeRegExpInternal(character);
        }
    }
    return new RegExp(`^${pattern}$`);
}

/** @brief glob 의 와일드카드 앞 고정 폴더입니다(`Source/**\/*.cpp` → `Source`). 그 아래만 걷습니다. */
function getGlobBaseFolder(glob) {
    const listSegment = glob.split('/');
    const listBase = [];
    for (const segment of listSegment.slice(0, -1)) {
        if (/[*?{]/.test(segment))
            break;
        listBase.push(segment);
    }
    return listBase.join('/');
}

module.exports = {
    makeEmptyProfile,
    normalizeProfile,
    loadProfile,
    hasCatalogSource,
    collectCacheVariableNames,
    makeGlobRegExp,
    getGlobBaseFolder,
};
