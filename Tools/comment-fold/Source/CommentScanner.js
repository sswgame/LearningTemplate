'use strict';

/**
 * @file CommentScanner.js
 * @brief C · C++ · HLSL 글에서 주석을 찾고, 접을 범위(줄 번호)를 고릅니다. VS Code 에 기대지 않아 단위 테스트가 그대로 부릅니다.
 * @details 정규식 대신 한 번 훑는 토크나이저입니다 — 문자열 · 문자 리터럴 · 원시 문자열(`R"d(...)d"`) 안의 `//` · `/*` 는 주석이 아니고,
 *          `1'000` 같은 자릿수 구분자는 문자 리터럴이 아닙니다. 줄 잇기(`\` + 줄 바꿈)는 `//` 주석과 문자열을 다음 줄로 늘립니다.
 *          줄 번호는 0 부터(VS Code `FoldingRange` 와 같은)입니다. CRLF 의 `\r` 은 공백으로 다룹니다.
 */

const kNewLine = 10;       // \n
const kCarriageReturn = 13; // \r
const kSlash = 47;         // /
const kStar = 42;          // *
const kBackslash = 92;     // \
const kDoubleQuote = 34;   // "
const kSingleQuote = 39;   // '
const kHash = 35;          // #
const kDot = 46;           // .
const kExclamation = 33;   // !
const kOpenParen = 40;     // (

/** @brief 원시 문자열 구분자 최대 길이입니다(C++ 표준 [lex.string]). */
const kRawDelimiterMaxLength = 16;
/** @brief 원시 문자열 접두사입니다(`R` 앞의 인코딩 접두사 포함). */
const kSetRawPrefix = new Set(['R', 'LR', 'uR', 'UR', 'u8R']);
/** @brief 파일 머리로 보는 Doxygen 태그입니다. */
const kFileTagPattern = /[@\\]file\b/;
/** @brief 파일 머리를 끝내지 않는 지시문입니다(`#pragma once` 뒤에 `@file` 블록이 오는 흔한 모양). */
const kPragmaOncePattern = /#[ \t]*pragma[ \t]+once\b/y;

function isSpaceCode(code) {
    return code === 32 || code === 9 || code === kCarriageReturn || code === 11 || code === 12;
}

function isIdentifierCode(code) {
    return (code >= 48 && code <= 57) || (code >= 65 && code <= 90) || (code >= 97 && code <= 122) || code === 95 || code > 127;
}

function isDigitCode(code) {
    return code >= 48 && code <= 57;
}

/**
 * @brief 글의 주석을 모두 찾습니다.
 * @return `{ listComment, firstCodeLine }`.
 *         `listComment` 의 원소는 `{ startLine, endLine, isLine, isDoc, isOwnLine, hasCodeAfter, text }` 입니다.
 *         - `isLine`: `//` 주석이면 참, `/* *\/` 이면 거짓.
 *         - `isDoc`: `///` · `//!` · `/**` · `/*!` 문서 주석이면 참(`////` · `/**\/` 는 아님 — Doxygen 규칙).
 *         - `isOwnLine`: 시작 줄에서 주석 앞에 코드가 없으면 참(줄 끝 주석이 아님).
 *         - `hasCodeAfter`: 끝 줄에서 주석 뒤에 코드가 있으면 참.
 *         `firstCodeLine` 은 주석 · `#pragma once` 가 아닌 첫 토큰의 줄이고, 없으면 -1 입니다.
 */
function scanComments(text) {
    const listComment = [];
    const length = text.length;
    let line = 0;
    let lastCodeLine = -1;
    let firstCodeLine = -1;
    let index = 0;

    /** @brief 코드 토큰 하나가 `tokenLine` 에서 시작했음을 적습니다. */
    const markCode = (tokenLine) => {
        lastCodeLine = tokenLine;
        if (firstCodeLine < 0)
            firstCodeLine = tokenLine;
        for (let back = listComment.length - 1; back >= 0 && listComment[back].endLine === tokenLine; --back)
            listComment[back].hasCodeAfter = true;
    };

    /** @brief `index` 의 `\` 가 줄 잇기이면 줄 바꿈 뒤 위치를, 아니면 -1 을 돌려줍니다. */
    const skipLineSplice = (at) => {
        let next = at + 1;
        if (next < length && text.charCodeAt(next) === kCarriageReturn)
            ++next;
        return next < length && text.charCodeAt(next) === kNewLine ? next + 1 : -1;
    };

    /** @brief `quote` 로 끝나는 보통 문자열 · 문자 리터럴을 건너뜁니다. 닫히지 않으면 줄 끝(잇기 없는)에서 멈춥니다. */
    const skipQuoted = (quote) => {
        ++index;
        while (index < length) {
            const code = text.charCodeAt(index);
            if (code === kBackslash) {
                const spliceEnd = skipLineSplice(index);
                if (spliceEnd >= 0) {
                    ++line;
                    index = spliceEnd;
                } else {
                    index += 2;
                }
                continue;
            }
            if (code === kNewLine)
                return;
            ++index;
            if (code === quote)
                return;
        }
    };

    while (index < length) {
        const code = text.charCodeAt(index);
        if (code === kNewLine) {
            ++line;
            ++index;
            continue;
        }
        if (isSpaceCode(code)) {
            ++index;
            continue;
        }
        if (code === kSlash && index + 1 < length) {
            const nextCode = text.charCodeAt(index + 1);
            if (nextCode === kSlash || nextCode === kStar) {
                const startIndex = index;
                const startLine = line;
                const isLine = nextCode === kSlash;
                if (isLine) {
                    index += 2;
                    while (index < length) {
                        const commentCode = text.charCodeAt(index);
                        if (commentCode === kNewLine)
                            break;
                        if (commentCode === kBackslash) {
                            const spliceEnd = skipLineSplice(index);
                            if (spliceEnd >= 0) {
                                ++line;
                                index = spliceEnd;
                                continue;
                            }
                        }
                        ++index;
                    }
                } else {
                    const closeIndex = text.indexOf('*/', index + 2);
                    const endIndex = closeIndex < 0 ? length : closeIndex + 2;
                    for (let scan = index; scan < endIndex; ++scan) {
                        if (text.charCodeAt(scan) === kNewLine)
                            ++line;
                    }
                    index = endIndex;
                }
                const commentText = text.slice(startIndex, index);
                const markerCode = commentText.charCodeAt(2);
                const isDoc = isLine
                    ? (markerCode === kSlash && commentText.charCodeAt(3) !== kSlash) || markerCode === kExclamation
                    : (markerCode === kStar && commentText !== '/**/' && commentText.charCodeAt(3) !== kSlash) || markerCode === kExclamation;
                listComment.push({ startLine, endLine: line, isLine, isDoc, isOwnLine: lastCodeLine !== startLine, hasCodeAfter: false, text: commentText });
                continue;
            }
        }

        markCode(line);
        if (code === kDoubleQuote || code === kSingleQuote) {
            skipQuoted(code);
            continue;
        }
        if (code === kHash && firstCodeLine === line) {
            kPragmaOncePattern.lastIndex = index;
            if (kPragmaOncePattern.test(text)) {
                // `#pragma once` 는 파일 머리를 끝내지 않는다 — 코드 줄로는 적되 첫 코드 줄로는 세지 않는다.
                firstCodeLine = -1;
                index = kPragmaOncePattern.lastIndex;
                continue;
            }
        }
        if (isDigitCode(code) || (code === kDot && index + 1 < length && isDigitCode(text.charCodeAt(index + 1)))) {
            // pp-number: 자릿수 구분자 `'` 와 지수 부호(`1e+5`)까지 한 토큰이다.
            ++index;
            while (index < length) {
                const numberCode = text.charCodeAt(index);
                const previousCode = text.charCodeAt(index - 1) | 32;
                if (isIdentifierCode(numberCode) || numberCode === kDot)
                    ++index;
                else if (numberCode === kSingleQuote && index + 1 < length && isIdentifierCode(text.charCodeAt(index + 1)))
                    index += 2;
                else if ((numberCode === 43 || numberCode === 45) && (previousCode === 101 || previousCode === 112))
                    ++index;
                else
                    break;
            }
            continue;
        }
        if (isIdentifierCode(code)) {
            const startIndex = index;
            while (index < length && isIdentifierCode(text.charCodeAt(index)))
                ++index;
            if (index < length && text.charCodeAt(index) === kDoubleQuote && kSetRawPrefix.has(text.slice(startIndex, index))) {
                const openIndex = text.indexOf('(', index + 1);
                const delimiter = openIndex < 0 ? '' : text.slice(index + 1, openIndex);
                if (openIndex >= 0 && delimiter.length <= kRawDelimiterMaxLength && /^[^\s\\()]*$/.test(delimiter)) {
                    const closeIndex = text.indexOf(`)${delimiter}"`, openIndex + 1);
                    const endIndex = closeIndex < 0 ? length : closeIndex + delimiter.length + 2;
                    for (let scan = index; scan < endIndex; ++scan) {
                        if (text.charCodeAt(scan) === kNewLine)
                            ++line;
                    }
                    index = endIndex;
                }
            }
            continue;
        }
        if (code === kBackslash) {
            const spliceEnd = skipLineSplice(index);
            if (spliceEnd >= 0) {
                ++line;
                index = spliceEnd;
                continue;
            }
        }
        ++index;
    }
    return { listComment, firstCodeLine };
}

/**
 * @brief 주석 목록에서 접을 범위를 고릅니다.
 * @param options `{ minimumLineCount, excludeFileHeader }` — 둘 다 반드시 줍니다(설정 기본값은 package.json 한 곳).
 * @return `[{ start, end }]`(0 부터, start 순). 접으면 start 줄만 보이고 start+1 … end 줄이 숨습니다.
 * @details 접는 단위:
 *          - 블록 주석(`/* *\/` · `/** *\/`) 하나.
 *          - 이어진 `//` 줄 묶음. 빈 줄 · 코드 · 블록 주석 · 문서 여부(`///` · `//!` ↔ `//`)가 바뀌는 곳에서 끊긴다.
 *          - 줄 끝 주석(앞에 코드가 있는 주석)은 접지 않는다 — 그 줄에서 시작하는 코드 블록 대신 주석을 고를 수 없다.
 *          - 끝 줄에 코드가 이어지면(`*\/ int x;`) 그 줄은 숨기지 않는다.
 */
function collectFoldRanges(scanResult, options) {
    if (!Number.isInteger(options.minimumLineCount) || typeof options.excludeFileHeader !== 'boolean')
        throw new Error('collectFoldRanges: options.minimumLineCount (integer) and options.excludeFileHeader (boolean) are required');
    const minimumLineCount = Math.max(2, options.minimumLineCount);
    const listRange = [];
    const { listComment, firstCodeLine } = scanResult;

    const isHeader = (comment) => options.excludeFileHeader
        && ((firstCodeLine < 0 || comment.endLine < firstCodeLine) || kFileTagPattern.test(comment.text));
    const pushRange = (start, end, listMember) => {
        if (end - start + 1 < minimumLineCount || listMember.some(isHeader))
            return;
        listRange.push({ start, end });
    };

    let index = 0;
    while (index < listComment.length) {
        const comment = listComment[index];
        if (!comment.isOwnLine) {
            ++index;
            continue;
        }
        if (!comment.isLine) {
            pushRange(comment.startLine, comment.hasCodeAfter ? comment.endLine - 1 : comment.endLine, [comment]);
            ++index;
            continue;
        }
        let last = index;
        while (last + 1 < listComment.length) {
            const next = listComment[last + 1];
            const current = listComment[last];
            if (!next.isLine || !next.isOwnLine || next.isDoc !== comment.isDoc || next.startLine !== current.endLine + 1 || current.hasCodeAfter)
                break;
            ++last;
        }
        const lastComment = listComment[last];
        pushRange(comment.startLine, lastComment.hasCodeAfter ? lastComment.endLine - 1 : lastComment.endLine, listComment.slice(index, last + 1));
        index = last + 1;
    }
    return listRange;
}

module.exports = {
    scanComments,
    collectFoldRanges,
};
