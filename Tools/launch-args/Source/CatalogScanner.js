'use strict';

/**
 * @file CatalogScanner.js
 * @brief 프로필(`CatalogProfile`)이 가리키는 소스를 읽어 "넘길 수 있는 것" 의 목록(카탈로그)을 만듭니다.
 * @details 두 가지를 읽습니다. 무엇을 어떻게 읽을지는 전부 프로필에 있고, 이 모듈은 특정 프로젝트를 모릅니다.
 *          - 전역 변수: `global_variable.files` 의 소스에서 `global_variable.macros` 호출. 인자 위치(타입 · 이름 · 기본값 · 설명)는
 *            `argument_index` 가 정합니다. 타입이 `type_map` 에 없으면 같은 이름의 `enum class` 정의를 찾아 열거자를 읽습니다.
 *          - 인자: `argument.files` 에서 `argument.macro` 호출(이름 · 기본값 · 철자들). 서로 배타인 인자 묶음(`exclusive_groups`)은
 *            이름 목록으로 적거나, 표 매크로 자리에 JSON 표의 줄을 펼쳐 넣습니다.
 *          파일 하나의 결과를 따로 들고 있어서, 소스가 바뀌면 그 파일만 다시 읽습니다(`rescanFile`).
 *          vscode 를 모르는 순수 모듈입니다(`Test/CatalogScannerTest.js`).
 */

const fs = require('fs');
const path = require('path');

const CatalogProfile = require('./CatalogProfile');
const CppTextUtil = require('./CppTextUtil');

/** @brief 값 편집기의 종류입니다. 전역 변수 · 인자가 같이 씁니다. */
const ValueKind = Object.freeze({
    Boolean: 'bool',
    Integer: 'int',
    Float: 'float',
    String: 'string',
    Enum: 'enum',
});

/** @brief 배타 묶음에 들지 않은 인자의 묶음 id 입니다. */
const kGeneralGroup = '';

const kMapLiteralToValueKind = new Map([
    [CppTextUtil.LiteralKind.Boolean, ValueKind.Boolean],
    [CppTextUtil.LiteralKind.Integer, ValueKind.Integer],
    [CppTextUtil.LiteralKind.Float, ValueKind.Float],
    [CppTextUtil.LiteralKind.String, ValueKind.String],
]);
const kEnumDefinitionRe = /\benum\s+(?:class|struct)\s+([A-Za-z_]\w*)\s*(?::\s*[A-Za-z_][\w:\s]*?)?\s*\{/g;
const kReadConcurrency = 32;
/** @brief 걷지 않는 폴더 이름입니다(빌드 산출물 · 내려받은 것). */
const kSkippedFolderNameSet = new Set(['.git', 'node_modules', 'build', 'out', '.vs', '.vscode']);

/** @brief 정규식 특수 글자를 이스케이프합니다. */
function escapeRegExpInternal(text) {
    return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** @brief 매크로 이름 목록에 맞는 호출 정규식(`g`, 긴 이름 먼저)입니다. */
function makeCallRegExpInternal(listMacroName) {
    const listSorted = listMacroName.filter((name) => name !== '').sort((left, right) => right.length - left.length);
    if (listSorted.length === 0)
        return null;
    return new RegExp(`\\b(${listSorted.map(escapeRegExpInternal).join('|')})\\s*\\(`, 'g');
}

/** @brief 경로를 저장소 루트 기준 `/` 구분 글로 바꿉니다. */
function makeRelativePathInternal(workspaceRoot, absolutePath) {
    return path.relative(workspaceRoot, absolutePath).split(path.sep).join('/');
}

/**
 * @brief 소스 경로로 모듈과 변형(활성일 때만 빌드되는 폴더 — 게임 · 플러그인)을 정합니다.
 * @return `{ moduleName, variantName, variantCacheVariable }`
 */
function classifyModuleInternal(relativePath, moduleProfile) {
    const result = { moduleName: '', variantName: '', variantCacheVariable: '' };
    const rootPrefix = moduleProfile.root === '' ? '' : `${moduleProfile.root}/`;
    if (relativePath.startsWith(rootPrefix) === false)
        return result;
    const listPart = relativePath.slice(rootPrefix.length).split('/');
    if (listPart.length < 2)
        return result;
    result.moduleName = listPart[0];
    const variant = moduleProfile.listVariantFolder.find((item) => item.folder === listPart[0]);
    if (variant !== undefined && listPart.length > 2) {
        result.variantName = listPart[1];
        result.variantCacheVariable = variant.cacheVariable;
    }
    return result;
}

/** @brief `enum class` 몸통 글에서 열거자 이름을 순서대로 뽑습니다. */
function parseEnumeratorListInternal(bodyText) {
    const listEnumerator = [];
    for (const itemText of bodyText.split(',')) {
        const match = /^\s*([A-Za-z_]\w*)/.exec(itemText);
        if (match !== null)
            listEnumerator.push(match[1]);
    }
    return listEnumerator;
}

/**
 * @brief 소스 파일 하나의 글에서 전역 변수 정의와 `enum class` 정의를 읽습니다.
 * @return `{ listGlobalVariable, listEnum, listProblem }`
 */
function parseSourceText(relativePath, text, profile) {
    const result = { listGlobalVariable: [], listEnum: [], listProblem: [] };
    const globalProfile = profile.globalVariable;
    const callRe = makeCallRegExpInternal(globalProfile.listMacro.map((macro) => macro.name));
    const bHasCall = callRe !== null && globalProfile.listMacro.some((macro) => text.includes(macro.name));
    const bHasEnum = /\benum\s+(?:class|struct)\b/.test(text);
    if (bHasCall === false && bHasEnum === false)
        return result;

    const blankedText = CppTextUtil.blankComments(text);
    const moduleInfo = classifyModuleInternal(relativePath, profile.module);
    const index = globalProfile.index;
    const requiredCount = Math.max(index.type, index.name, index.defaultValue) + 1;
    for (const call of bHasCall ? CppTextUtil.findMacroCalls(blankedText, callRe) : []) {
        if (call.listArgument.length < requiredCount) {
            result.listProblem.push(`${relativePath}:${call.lineNumber}: ${call.macroName} has ${call.listArgument.length} arguments, the profile reads index ${requiredCount - 1}`);
            continue;
        }
        const typeName = CppTextUtil.normalizeTypeName(call.listArgument[index.type]);
        const defaultLiteral = CppTextUtil.parseLiteral(call.listArgument[index.defaultValue]);
        const descriptionText = index.description < call.listArgument.length ? call.listArgument.slice(index.description).join(', ') : '';
        const description = CppTextUtil.parseStringLiteralSequence(descriptionText);
        const macro = globalProfile.listMacro.find((item) => item.name === call.macroName);
        result.listGlobalVariable.push({
            name: call.listArgument[index.name],
            typeName,
            valueKind: Object.prototype.hasOwnProperty.call(globalProfile.mapType, typeName) ? globalProfile.mapType[typeName] : ValueKind.Enum,
            defaultText: defaultLiteral.value,
            defaultLiteralKind: defaultLiteral.kind,
            description: description === null ? descriptionText : description,
            tag: macro === undefined ? '' : macro.tag,
            moduleName: moduleInfo.moduleName,
            variantName: moduleInfo.variantName,
            variantCacheVariable: moduleInfo.variantCacheVariable,
            relativePath,
            lineNumber: call.lineNumber,
            listEnumerator: [],
        });
    }

    kEnumDefinitionRe.lastIndex = 0;
    for (let match = kEnumDefinitionRe.exec(blankedText); match !== null; match = kEnumDefinitionRe.exec(blankedText)) {
        const openBraceIndex = match.index + match[0].length - 1;
        const splitResult = CppTextUtil.splitMacroArguments(blankedText, openBraceIndex);
        if (splitResult === null)
            continue;
        result.listEnum.push({
            name: match[1],
            listEnumerator: parseEnumeratorListInternal(blankedText.slice(openBraceIndex + 1, splitResult.closeIndex)),
            relativePath,
        });
    }
    return result;
}

/**
 * @brief 인자 호출 위에 붙은 설명 주석을 찾습니다.
 * @details 바로 위의 같은 매크로 줄들(한 무리)을 건너뛰고, 그 위로 이어지는 `//` 블록을 씁니다. @p maxCommentLine 줄을 넘는
 *          블록은 개별 인자가 아니라 표 전체에 대한 설명이라 붙이지 않습니다.
 */
function findArgumentCommentInternal(listLine, lineIndex, macroName, maxCommentLine) {
    const siblingRe = new RegExp(`^\\s*${escapeRegExpInternal(macroName)}\\s*\\(`);
    let index = lineIndex - 1;
    while (0 <= index && siblingRe.test(listLine[index]))
        index -= 1;
    const listComment = [];
    while (0 <= index && /^\s*\/\//.test(listLine[index])) {
        listComment.unshift(listLine[index].replace(/^\s*\/\/\s?/, '').trim());
        index -= 1;
    }
    if (listComment.length === 0 || maxCommentLine < listComment.length)
        return '';
    return listComment.join(' ');
}

/**
 * @brief 인자 파일 하나의 글에서 인자 목록을 읽습니다. 순서는 파일 순서입니다.
 * @param mapJson 배타 묶음이 쓰는 JSON 파일(저장소 기준 경로 → 읽은 객체, 못 읽었으면 null)입니다.
 * @return `{ listArgument, listProblem }`
 */
function parseArgumentText(relativePath, text, profile, mapJson) {
    const result = { listArgument: [], listProblem: [] };
    const argumentProfile = profile.argument;
    const listTableGroup = argumentProfile.listExclusiveGroup.filter((group) => group.tableMacro !== '');
    const callRe = makeCallRegExpInternal([argumentProfile.macroName].concat(listTableGroup.map((group) => group.tableMacro)));
    if (callRe === null)
        return result;
    const listLine = text.split(/\r?\n/);
    const index = argumentProfile.index;
    for (const call of CppTextUtil.findMacroCalls(CppTextUtil.blankComments(text), callRe)) {
        const tableGroup = listTableGroup.find((group) => group.tableMacro === call.macroName);
        if (tableGroup !== undefined) {
            const json = mapJson.get(tableGroup.file);
            const listRow = json !== undefined && json !== null && Array.isArray(json[tableGroup.rowsKey]) ? json[tableGroup.rowsKey] : null;
            if (listRow === null) {
                result.listProblem.push(`${tableGroup.file}: "${tableGroup.rowsKey}" not found - arguments of group ${tableGroup.id} are missing`);
                continue;
            }
            for (const row of listRow) {
                // 철자 칸은 문자열 하나(`command_line_name`)이거나 문자열 배열이다.
                const spellingValue = row[tableGroup.spellingsKey];
                const listSpelling = Array.isArray(spellingValue) ? spellingValue.filter((item) => typeof item === 'string')
                    : (typeof spellingValue === 'string' ? [spellingValue] : []);
                if (typeof row[tableGroup.nameKey] !== 'string' || listSpelling.length === 0) {
                    result.listProblem.push(`${tableGroup.file}: a "${tableGroup.rowsKey}" row lacks "${tableGroup.nameKey}" or "${tableGroup.spellingsKey}"`);
                    continue;
                }
                const label = tableGroup.labelKey !== '' && typeof row[tableGroup.labelKey] === 'string' ? row[tableGroup.labelKey] : row[tableGroup.nameKey];
                result.listArgument.push({
                    name: row[tableGroup.nameKey], listSpelling, valueKind: ValueKind.Boolean, defaultText: 'false',
                    description: '', group: tableGroup.id, label, relativePath: tableGroup.file, lineNumber: 1,
                });
            }
            continue;
        }
        const requiredCount = Math.max(index.name, index.defaultValue, index.spellingStart) + 1;
        if (call.listArgument.length < requiredCount) {
            result.listProblem.push(`${relativePath}:${call.lineNumber}: ${call.macroName} has ${call.listArgument.length} arguments, the profile reads index ${requiredCount - 1}`);
            continue;
        }
        const defaultLiteral = CppTextUtil.parseLiteral(call.listArgument[index.defaultValue]);
        const listSpelling = [];
        for (const spellingText of call.listArgument.slice(index.spellingStart)) {
            const spelling = CppTextUtil.parseStringLiteralSequence(spellingText);
            if (spelling !== null && spelling !== '')
                listSpelling.push(spelling);
        }
        if (listSpelling.length === 0 || kMapLiteralToValueKind.has(defaultLiteral.kind) === false) {
            result.listProblem.push(`${relativePath}:${call.lineNumber}: cannot read ${call.listArgument[index.name]} (spelling or default value)`);
            continue;
        }
        const name = call.listArgument[index.name];
        result.listArgument.push({
            name,
            listSpelling,
            valueKind: kMapLiteralToValueKind.get(defaultLiteral.kind),
            defaultText: defaultLiteral.value,
            description: findArgumentCommentInternal(listLine, call.lineNumber - 1, argumentProfile.macroName, argumentProfile.maxCommentLine),
            group: kGeneralGroup,
            label: name,
            relativePath,
            lineNumber: call.lineNumber,
        });
    }
    return result;
}

/** @brief 전역 변수를 화면 · 명령줄에 늘어놓는 순서로 비교합니다: 이름, 같으면 변형 이름. */
function compareGlobalVariableInternal(left, right) {
    if (left.name !== right.name)
        return left.name < right.name ? -1 : 1;
    if (left.variantName !== right.variantName)
        return left.variantName < right.variantName ? -1 : 1;
    return 0;
}

/**
 * @brief 작업 폴더 하나의 카탈로그를 만들고 소스가 바뀌면 고칩니다.
 * @details 파일 하나의 결과를 `_mapFileResult` 에 들고 있다가 `makeCatalog` 가 합칩니다. 그래서 저장 한 번에 그 파일만 다시 읽습니다.
 */
class CatalogScanner {
    /**
     * @param workspaceRoot 작업 폴더 루트(절대 경로)입니다.
     * @param profile `CatalogProfile.normalizeProfile` 의 결과입니다.
     */
    constructor(workspaceRoot, profile) {
        /** @brief 작업 폴더 루트 절대 경로입니다. */
        this._workspaceRoot = workspaceRoot;
        /** @brief 프로필입니다. */
        this._profile = profile;
        /** @brief 전역 변수 소스 glob 의 정규식입니다. */
        this._listGlobalFileRe = profile.globalVariable.listFileGlob.map(CatalogProfile.makeGlobRegExp);
        /** @brief 인자 파일 glob 의 정규식입니다. */
        this._listArgumentFileRe = profile.argument.listFileGlob.map(CatalogProfile.makeGlobRegExp);
        /** @brief 프로필이 읽는 JSON 파일(저장소 기준 경로)입니다. 바뀌면 인자 · 기본값을 다시 읽습니다. */
        this._uniqueJsonFile = new Set();
        for (const group of profile.argument.listExclusiveGroup) {
            if (group.file !== '')
                this._uniqueJsonFile.add(group.file);
        }
        for (const source of Object.values(profile.globalVariable.mapDefaultValueMacro)) {
            if (source.file !== '')
                this._uniqueJsonFile.add(source.file);
        }
        /** @brief 저장소 기준 소스 경로 → 그 파일에서 읽은 `{ listGlobalVariable, listEnum, listProblem }` 입니다. */
        this._mapFileResult = new Map();
        /** @brief 인자 파일들과 JSON 표에서 읽은 결과입니다. */
        this._argumentResult = { listArgument: [], listProblem: [] };
        /** @brief 저장소 기준 JSON 경로 → 읽은 객체(못 읽었으면 null)입니다. */
        this._mapJson = new Map();
    }

    /** @brief 감시할 glob 목록입니다(소스 · 인자 파일 · JSON 표). */
    getWatchGlobList() {
        return this._profile.globalVariable.listFileGlob.concat(this._profile.argument.listFileGlob, Array.from(this._uniqueJsonFile));
    }

    /** @brief 소스 전체와 인자 파일을 처음부터 다시 읽습니다. */
    async scanAll() {
        const listFile = await this._collectFiles(this._profile.globalVariable.listFileGlob, this._listGlobalFileRe);
        const mapFileResult = new Map();
        let nextIndex = 0;
        const readNext = async () => {
            while (nextIndex < listFile.length) {
                const relativePath = listFile[nextIndex];
                nextIndex += 1;
                const fileResult = await this._parseSourceFile(relativePath);
                if (fileResult !== null)
                    mapFileResult.set(relativePath, fileResult);
            }
        };
        const listWorker = [];
        for (let workerIndex = 0; workerIndex < kReadConcurrency; workerIndex += 1)
            listWorker.push(readNext());
        await Promise.all(listWorker);
        this._mapFileResult = mapFileResult;
        await this._scanArguments();
    }

    /**
     * @brief 바뀐 파일 하나를 다시 읽습니다. 프로필이 읽지 않는 파일이면 아무것도 하지 않습니다.
     * @return 카탈로그에 닿는 파일이었으면 true 입니다(부르는 쪽이 `makeCatalog` 로 새 목록을 얻습니다).
     */
    async rescanFile(absolutePath) {
        const relativePath = makeRelativePathInternal(this._workspaceRoot, absolutePath);
        const bArgumentFile = this._listArgumentFileRe.some((fileRe) => fileRe.test(relativePath));
        if (bArgumentFile || this._uniqueJsonFile.has(relativePath)) {
            await this._scanArguments();
            return true;
        }
        if (this._listGlobalFileRe.some((fileRe) => fileRe.test(relativePath)) === false)
            return false;
        const fileResult = await this._parseSourceFile(relativePath);
        const bHadResult = this._mapFileResult.has(relativePath);
        if (fileResult === null)
            this._mapFileResult.delete(relativePath);
        else
            this._mapFileResult.set(relativePath, fileResult);
        return bHadResult || fileResult !== null;
    }

    /**
     * @brief 읽어 둔 결과를 합쳐 카탈로그를 만듭니다.
     * @return `{ listGlobalVariable, listTag, listArgument, listExclusiveGroup, listProblem }` — 전역 변수는 이름 순, 인자는 파일 순,
     *         `listTag` 는 프로필 매크로 순서의 `[{ tag, label, count }]`(같은 태그는 하나, 빈 태그 = 일반)입니다.
     */
    makeCatalog() {
        const listGlobalVariable = [];
        const mapEnum = new Map();
        const listProblem = [];
        for (const fileResult of this._mapFileResult.values()) {
            listGlobalVariable.push(...fileResult.listGlobalVariable);
            listProblem.push(...fileResult.listProblem);
            for (const enumInfo of fileResult.listEnum) {
                if (mapEnum.has(enumInfo.name) === false)
                    mapEnum.set(enumInfo.name, []);
                mapEnum.get(enumInfo.name).push(enumInfo);
            }
        }
        listGlobalVariable.sort(compareGlobalVariableInternal);

        const mapSeenName = new Map();
        for (const variable of listGlobalVariable) {
            const seenKey = `${variable.name}|${variable.variantName}`;
            if (mapSeenName.has(seenKey))
                listProblem.push(`${variable.relativePath}:${variable.lineNumber}: ${variable.name} is also defined at ${mapSeenName.get(seenKey)}`);
            else
                mapSeenName.set(seenKey, `${variable.relativePath}:${variable.lineNumber}`);
            this._resolveDefaultValue(variable);
            if (variable.valueKind === ValueKind.Enum)
                this._resolveEnum(variable, mapEnum, listProblem);
        }

        const listTag = [];
        for (const macro of this._profile.globalVariable.listMacro) {
            if (listTag.some((item) => item.tag === macro.tag) === false)
                listTag.push({ tag: macro.tag, label: macro.label, count: listGlobalVariable.filter((variable) => variable.tag === macro.tag).length });
        }

        const listExclusiveGroup = [];
        for (const group of this._profile.argument.listExclusiveGroup) {
            const json = this._mapJson.get(group.file);
            const defaultLabel = group.defaultKey !== '' && json !== undefined && json !== null && typeof json[group.defaultKey] === 'string' ? json[group.defaultKey] : '';
            listExclusiveGroup.push({ id: group.id, label: group.label, defaultLabel });
        }
        return {
            listGlobalVariable,
            listTag,
            listArgument: this._argumentResult.listArgument,
            listExclusiveGroup,
            listProblem: listProblem.concat(this._argumentResult.listProblem),
        };
    }

    /** @brief glob 목록에 맞는 파일(저장소 기준 경로)을 경로 순으로 모읍니다. glob 의 고정 폴더 아래만 걷습니다. */
    async _collectFiles(listGlob, listFileRe) {
        const uniqueBase = new Set(listGlob.map(CatalogProfile.getGlobBaseFolder));
        const uniqueFile = new Set();
        for (const baseFolder of uniqueBase) {
            const listPending = [path.join(this._workspaceRoot, ...baseFolder.split('/').filter((part) => part !== ''))];
            while (listPending.length > 0) {
                const directoryPath = listPending.pop();
                let listDirent;
                try {
                    listDirent = await fs.promises.readdir(directoryPath, { withFileTypes: true });
                } catch (error) {
                    continue;
                }
                for (const dirent of listDirent) {
                    const entryPath = path.join(directoryPath, dirent.name);
                    if (dirent.isDirectory()) {
                        if (kSkippedFolderNameSet.has(dirent.name) === false)
                            listPending.push(entryPath);
                        continue;
                    }
                    const relativePath = makeRelativePathInternal(this._workspaceRoot, entryPath);
                    if (dirent.isFile() && listFileRe.some((fileRe) => fileRe.test(relativePath)))
                        uniqueFile.add(relativePath);
                }
            }
        }
        return Array.from(uniqueFile).sort();
    }

    /** @brief 소스 파일 하나를 읽어 해석합니다. 담긴 것이 없거나 읽지 못하면 null 입니다. */
    async _parseSourceFile(relativePath) {
        let text;
        try {
            text = await fs.promises.readFile(path.join(this._workspaceRoot, ...relativePath.split('/')), 'utf8');
        } catch (error) {
            return null;
        }
        const fileResult = parseSourceText(relativePath, text, this._profile);
        const bEmpty = fileResult.listGlobalVariable.length === 0 && fileResult.listEnum.length === 0 && fileResult.listProblem.length === 0;
        return bEmpty ? null : fileResult;
    }

    /** @brief JSON 표와 인자 파일을 다시 읽습니다. */
    async _scanArguments() {
        const listProblem = [];
        this._mapJson = new Map();
        for (const relativePath of this._uniqueJsonFile) {
            try {
                this._mapJson.set(relativePath, JSON.parse(await fs.promises.readFile(path.join(this._workspaceRoot, ...relativePath.split('/')), 'utf8')));
            } catch (error) {
                this._mapJson.set(relativePath, null);
                listProblem.push(`${relativePath}: ${error.message}`);
            }
        }
        const listArgument = [];
        for (const relativePath of await this._collectFiles(this._profile.argument.listFileGlob, this._listArgumentFileRe)) {
            let text;
            try {
                text = await fs.promises.readFile(path.join(this._workspaceRoot, ...relativePath.split('/')), 'utf8');
            } catch (error) {
                listProblem.push(`${relativePath}: ${error.message}`);
                continue;
            }
            const fileResult = parseArgumentText(relativePath, text, this._profile, this._mapJson);
            listArgument.push(...fileResult.listArgument);
            listProblem.push(...fileResult.listProblem);
        }
        // 이름 목록으로 적은 배타 묶음은 이미 읽은 인자에 묶음 id 를 붙인다.
        for (const group of this._profile.argument.listExclusiveGroup) {
            for (const argumentName of group.listArgumentName) {
                const argument = listArgument.find((item) => item.name === argumentName);
                if (argument === undefined)
                    listProblem.push(`exclusive group ${group.id}: argument ${argumentName} not found`);
                else if (argument.valueKind !== ValueKind.Boolean)
                    listProblem.push(`exclusive group ${group.id}: argument ${argumentName} is not a flag`);
                else
                    argument.group = group.id;
            }
        }
        this._argumentResult = { listArgument, listProblem };
    }

    /** @brief 기본값이 프로필 `default_value_macros` 의 매크로면 그 값으로 풉니다(`Ns::MACRO` 의 끝 토막으로 찾는다). */
    _resolveDefaultValue(variable) {
        const lastSegment = variable.defaultText.split('::').pop().trim();
        const source = this._profile.globalVariable.mapDefaultValueMacro[lastSegment];
        if (source === undefined)
            return;
        if (source.file === '') {
            variable.defaultText = source.value;
            return;
        }
        const json = this._mapJson.get(source.file);
        if (json !== undefined && json !== null && json[source.key] !== undefined)
            variable.defaultText = String(json[source.key]);
    }

    /**
     * @brief enum 전역 변수의 열거자 목록과 기본 열거자를 채웁니다.
     * @details 같은 이름의 정의가 여럿이면 열거자가 모두 같을 때만 씁니다(전방 선언은 몸통이 없어 세지 않는다).
     */
    _resolveEnum(variable, mapEnum, listProblem) {
        const listDefinition = mapEnum.has(variable.typeName) ? mapEnum.get(variable.typeName) : [];
        const uniqueSignature = new Set(listDefinition.map((definition) => definition.listEnumerator.join(',')));
        if (listDefinition.length === 0 || uniqueSignature.size !== 1) {
            const reason = listDefinition.length === 0 ? 'type is not in type_map and no enum definition was found' : 'enum is defined differently in several files';
            listProblem.push(`${variable.relativePath}:${variable.lineNumber}: ${variable.name}: ${reason} for ${variable.typeName} - edited as text`);
            variable.valueKind = ValueKind.String;
            return;
        }
        variable.listEnumerator = listDefinition[0].listEnumerator.slice();
        const lastSegment = variable.defaultText.split('::').pop().trim();
        if (variable.listEnumerator.includes(lastSegment))
            variable.defaultText = lastSegment;
    }
}

module.exports = {
    ValueKind,
    kGeneralGroup,
    CatalogScanner,
    parseSourceText,
    parseArgumentText,
};
