'use strict';

/**
 * @file CppTextUtil.js
 * @brief 컴파일러 없이 C++ 소스 글을 읽는 도구입니다 — 주석 지우기 · 매크로 호출 찾기 · 인자 나누기 · 리터럴 읽기.
 * @details 정본은 언제나 C++ 소스이고 이 모듈은 그 글을 **읽기만** 합니다. 읽지 못한 모양은 버리지 않고
 *          `LiteralKind.Expression` 으로 원문 그대로 돌려주어, 부르는 쪽이 "모른다" 를 화면에 보일 수 있게 합니다.
 *          vscode 를 모르는 순수 모듈이라 VS Code 의 node 로 바로 시험합니다(`Test/CppTextUtilTest.js`).
 */

/** @brief 리터럴 한 개를 읽은 결과의 종류입니다. */
const LiteralKind = Object.freeze({
    Boolean: 'bool',
    Integer: 'int',
    Float: 'float',
    String: 'string',
    Expression: 'expression',
});

const kIntegerLiteralRe = /^[+-]?\d+[uUlL]*$/;
const kFloatLiteralRe = /^[+-]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][+-]?\d+)?[fFlL]?$/;
const kStringLiteralRe = /^\s*(?:u8|u|U|L)?"((?:\\.|[^"\\\r\n])*)"/;
const kIdentifierCharRe = /[A-Za-z0-9_]/;
const kRawStringPrefixSet = new Set(['', 'u8', 'u', 'U', 'L']);
const kMapSimpleEscape = new Map([
    ['n', '\n'],
    ['t', '\t'],
    ['r', '\r'],
    ['0', '\0'],
    ['a', '\x07'],
    ['b', '\b'],
    ['f', '\f'],
    ['v', '\v'],
]);

/** @brief 글 조각을 줄바꿈(`\r` · `\n`)만 남기고 공백으로 바꿉니다. 지운 뒤에도 위치와 줄 번호가 그대로입니다. */
function blankKeepNewlineInternal(text) {
    return text.replace(/[^\r\n]/g, ' ');
}

/** @brief @p index 의 글자가 식별자를 이루는 글자인지 묻습니다. 범위 밖이면 false 입니다. */
function isIdentifierCharAtInternal(text, index) {
    return 0 <= index && index < text.length && kIdentifierCharRe.test(text[index]);
}

/**
 * @brief @p quoteIndex 의 따옴표로 시작하는 문자열 · 문자 리터럴을 건너뛴 다음 위치를 돌려줍니다.
 * @details 닫히지 않은 리터럴은 그 줄 끝에서 멈춥니다 — 한 줄의 오타가 파일 끝까지 삼키지 않게 합니다.
 */
function skipQuotedInternal(text, quoteIndex) {
    const quote = text[quoteIndex];
    let index = quoteIndex + 1;
    while (index < text.length) {
        const character = text[index];
        if (character === '\\') {
            index += 2;
            continue;
        }
        if (character === quote)
            return index + 1;
        if (character === '\n')
            return index;
        index += 1;
    }
    return text.length;
}

/** @brief @p index 의 `R"` 가 원시 문자열의 시작인지 묻습니다(`R"` · `u8R"` · `LR"` …, 식별자의 끝 글자 R 은 아닙니다). */
function isRawStringStartInternal(text, index) {
    if (text[index] !== 'R' || text[index + 1] !== '"')
        return false;
    let prefixStart = index;
    while (isIdentifierCharAtInternal(text, prefixStart - 1))
        prefixStart -= 1;
    return kRawStringPrefixSet.has(text.slice(prefixStart, index));
}

/** @brief @p index 의 `R"delim(` 로 시작하는 원시 문자열을 건너뛴 다음 위치를 돌려줍니다. */
function skipRawStringInternal(text, index) {
    const openParen = text.indexOf('(', index + 2);
    if (openParen < 0)
        return text.length;
    const closing = `)${text.slice(index + 2, openParen)}"`;
    const closeIndex = text.indexOf(closing, openParen + 1);
    return closeIndex < 0 ? text.length : closeIndex + closing.length;
}

/**
 * @brief 작은따옴표가 문자 리터럴을 여는지 묻습니다. `1'000` 의 자릿수 구분자는 아닙니다.
 * @details 앞 글자가 식별자 글자이면 구분자로 봅니다. 단 `u8'a'` · `L'a'` 처럼 앞 단어가 접두사이면 리터럴입니다.
 */
function isCharLiteralStartInternal(text, index) {
    if (isIdentifierCharAtInternal(text, index - 1) === false)
        return true;
    let wordStart = index;
    while (isIdentifierCharAtInternal(text, wordStart - 1))
        wordStart -= 1;
    const prefix = text.slice(wordStart, index);
    return prefix === 'u8' || prefix === 'u' || prefix === 'U' || prefix === 'L';
}

/**
 * @brief 주석(`//` · `/* *\/`)을 같은 길이의 공백으로 바꿉니다. 문자열 · 문자 · 원시 문자열 리터럴 안은 건드리지 않습니다.
 * @details 줄바꿈을 남기므로 지운 글의 위치 · 줄 번호가 원문과 같습니다. 그래서 찾은 자리를 원문에서 그대로 씁니다.
 */
function blankComments(text) {
    const listChunk = [];
    let chunkStart = 0;
    let index = 0;
    while (index < text.length) {
        const character = text[index];
        const nextCharacter = text[index + 1];
        if (character === '/' && nextCharacter === '/') {
            const newlineIndex = text.indexOf('\n', index);
            const commentEnd = newlineIndex < 0 ? text.length : newlineIndex;
            listChunk.push(text.slice(chunkStart, index), blankKeepNewlineInternal(text.slice(index, commentEnd)));
            index = commentEnd;
            chunkStart = commentEnd;
            continue;
        }
        if (character === '/' && nextCharacter === '*') {
            const closeIndex = text.indexOf('*/', index + 2);
            const commentEnd = closeIndex < 0 ? text.length : closeIndex + 2;
            listChunk.push(text.slice(chunkStart, index), blankKeepNewlineInternal(text.slice(index, commentEnd)));
            index = commentEnd;
            chunkStart = commentEnd;
            continue;
        }
        if (isRawStringStartInternal(text, index)) {
            index = skipRawStringInternal(text, index);
            continue;
        }
        if (character === '"' || (character === '\'' && isCharLiteralStartInternal(text, index))) {
            index = skipQuotedInternal(text, index);
            continue;
        }
        index += 1;
    }
    listChunk.push(text.slice(chunkStart));
    return listChunk.join('');
}

/** @brief @p index 가 든 물리 줄의 시작 위치입니다. */
function findLineStartInternal(text, index) {
    return text.lastIndexOf('\n', index - 1) + 1;
}

/** @brief 물리 줄 [lineStart, …) 의 바로 앞 줄이 `\` 로 이어지는지 묻습니다. */
function isContinuedFromPreviousLineInternal(text, lineStart) {
    if (lineStart === 0)
        return false;
    let index = lineStart - 2;   // lineStart - 1 은 '\n' 이다
    if (0 <= index && text[index] === '\r')
        index -= 1;
    return 0 <= index && text[index] === '\\';
}

/**
 * @brief @p index 가 전처리 지시문(`#define` 과 그 `\` 연속 줄 포함) 안에 있는지 묻습니다.
 * @details 매크로 정의 몸통의 `SW_REGISTER_ARGUMENT( ... )` 같은 글은 호출이 아니므로 세지 않습니다.
 */
function isInPreprocessorDirective(text, index) {
    let lineStart = findLineStartInternal(text, index);
    while (isContinuedFromPreviousLineInternal(text, lineStart))
        lineStart = findLineStartInternal(text, lineStart - 1);
    return /^[ \t]*#/.test(text.slice(lineStart, Math.min(text.length, lineStart + 256)));
}

/** @brief @p index 의 1 부터 세는 줄 번호입니다. */
function computeLineNumber(text, index) {
    let lineNumber = 1;
    let searchFrom = 0;
    for (;;) {
        const newlineIndex = text.indexOf('\n', searchFrom);
        if (newlineIndex < 0 || index <= newlineIndex)
            return lineNumber;
        lineNumber += 1;
        searchFrom = newlineIndex + 1;
    }
}

/**
 * @brief @p openParenIndex 의 여는 괄호부터 짝이 되는 닫는 괄호까지를 맨 위 쉼표로 나눕니다.
 * @details `( ) [ ] { }` 깊이와 리터럴을 따라가므로 설명 문자열 안의 쉼표 · 괄호는 나누지 않습니다. `< >` 는 따라가지 않습니다
 *          (비교 연산자와 구별할 수 없고, 다루는 매크로의 타입 인자에 템플릿이 오지 않습니다).
 *          @p text 는 주석을 지운 글이어야 합니다(`blankComments`).
 * @return `{ listArgument, closeIndex }` — 각 인자는 앞뒤 공백을 자른 원문입니다. 괄호가 닫히지 않으면 null 입니다.
 */
function splitMacroArguments(text, openParenIndex) {
    const listArgument = [];
    let depth = 0;
    let argumentStart = openParenIndex + 1;
    let index = openParenIndex;
    while (index < text.length) {
        const character = text[index];
        if (isRawStringStartInternal(text, index)) {
            index = skipRawStringInternal(text, index);
            continue;
        }
        if (character === '"' || (character === '\'' && isCharLiteralStartInternal(text, index))) {
            index = skipQuotedInternal(text, index);
            continue;
        }
        if (character === '(' || character === '[' || character === '{') {
            depth += 1;
        } else if (character === ')' || character === ']' || character === '}') {
            depth -= 1;
            if (depth === 0) {
                listArgument.push(text.slice(argumentStart, index).trim());
                return { listArgument, closeIndex: index };
            }
        } else if (character === ',' && depth === 1) {
            listArgument.push(text.slice(argumentStart, index).trim());
            argumentStart = index + 1;
        }
        index += 1;
    }
    return null;
}

/**
 * @brief 정규식 @p callRe(`g` 플래그, 첫 묶음이 매크로 이름, `\(` 로 끝남)에 맞는 매크로 호출을 모두 찾습니다.
 * @details 전처리 지시문 안의 글은 호출이 아니므로 건너뜁니다. @p blankedText 는 주석을 지운 글입니다.
 * @return `[{ macroName, listArgument, startIndex, lineNumber }]`
 */
function findMacroCalls(blankedText, callRe) {
    const listCall = [];
    callRe.lastIndex = 0;
    for (let match = callRe.exec(blankedText); match !== null; match = callRe.exec(blankedText)) {
        if (isInPreprocessorDirective(blankedText, match.index))
            continue;
        const openParenIndex = match.index + match[0].length - 1;
        const splitResult = splitMacroArguments(blankedText, openParenIndex);
        if (splitResult === null)
            continue;
        listCall.push({
            macroName: match[1],
            listArgument: splitResult.listArgument,
            startIndex: match.index,
            lineNumber: computeLineNumber(blankedText, match.index),
        });
        callRe.lastIndex = splitResult.closeIndex;
    }
    return listCall;
}

/** @brief 이스케이프 하나(`\` 다음 글)를 풀고 `{ text, length }` 를 돌려줍니다. 모르는 이스케이프는 글자 그대로입니다. */
function decodeEscapeInternal(body, backslashIndex) {
    const character = body[backslashIndex + 1];
    if (kMapSimpleEscape.has(character))
        return { text: kMapSimpleEscape.get(character), length: 2 };
    if (character === 'x') {
        const hexMatch = /^[0-9A-Fa-f]{1,2}/.exec(body.slice(backslashIndex + 2));
        if (hexMatch !== null)
            return { text: String.fromCharCode(parseInt(hexMatch[0], 16)), length: 2 + hexMatch[0].length };
    }
    if (character === 'u' || character === 'U') {
        const digitCount = character === 'u' ? 4 : 8;
        const hexText = body.slice(backslashIndex + 2, backslashIndex + 2 + digitCount);
        if (new RegExp(`^[0-9A-Fa-f]{${digitCount}}$`).test(hexText))
            return { text: String.fromCodePoint(parseInt(hexText, 16)), length: 2 + digitCount };
    }
    return { text: character === undefined ? '' : character, length: 2 };
}

/** @brief 문자열 리터럴 몸통(따옴표 안)의 이스케이프를 풉니다. */
function decodeStringBodyInternal(body) {
    const listPiece = [];
    let index = 0;
    while (index < body.length) {
        const backslashIndex = body.indexOf('\\', index);
        if (backslashIndex < 0) {
            listPiece.push(body.slice(index));
            break;
        }
        listPiece.push(body.slice(index, backslashIndex));
        const escape = decodeEscapeInternal(body, backslashIndex);
        listPiece.push(escape.text);
        index = backslashIndex + escape.length;
    }
    return listPiece.join('');
}

/**
 * @brief 이어 붙은 문자열 리터럴(`"a" "b"`, 줄을 넘어도 된다)을 하나로 읽습니다.
 * @return 풀린 글입니다. @p text 전체가 문자열 리터럴만으로 되어 있지 않으면 null 입니다.
 */
function parseStringLiteralSequence(text) {
    const listPiece = [];
    let rest = text;
    for (;;) {
        const match = kStringLiteralRe.exec(rest);
        if (match === null)
            break;
        listPiece.push(decodeStringBodyInternal(match[1]));
        rest = rest.slice(match[0].length);
    }
    if (listPiece.length === 0 || rest.trim() !== '')
        return null;
    return listPiece.join('');
}

/**
 * @brief 리터럴 하나를 읽습니다: `true` · 정수 · 실수 · 문자열, 그 밖은 원문 그대로 `Expression` 입니다.
 * @return `{ kind, value }` — `value` 는 글입니다(정수 · 실수는 접미사 `u` · `f` 를 뗀 숫자, 문자열은 풀린 글).
 */
function parseLiteral(text) {
    const trimmed = text.trim();
    if (trimmed === 'true' || trimmed === 'false')
        return { kind: LiteralKind.Boolean, value: trimmed };
    if (kIntegerLiteralRe.test(trimmed))
        return { kind: LiteralKind.Integer, value: trimmed.replace(/[uUlL]+$/, '') };
    if (kFloatLiteralRe.test(trimmed))
        return { kind: LiteralKind.Float, value: trimmed.replace(/[fFlL]$/, '') };
    const stringValue = parseStringLiteralSequence(trimmed);
    if (stringValue !== null)
        return { kind: LiteralKind.String, value: stringValue };
    return { kind: LiteralKind.Expression, value: trimmed };
}

/** @brief 타입 글에서 공백과 맨 앞의 `::` 를 뗍니다. 이름공간 별칭(`ns::string` → `string`)은 프로필 `type_map` 이 정합니다. */
function normalizeTypeName(typeText) {
    return typeText.replace(/\s+/g, '').replace(/^::/, '');
}

module.exports = {
    LiteralKind,
    blankComments,
    isInPreprocessorDirective,
    computeLineNumber,
    splitMacroArguments,
    findMacroCalls,
    parseStringLiteralSequence,
    parseLiteral,
    normalizeTypeName,
};
