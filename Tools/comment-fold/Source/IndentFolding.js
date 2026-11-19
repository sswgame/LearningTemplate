'use strict';

/**
 * @file IndentFolding.js
 * @brief 들여쓰기 기반 코드 접기 범위입니다 — VS Code 의 들여쓰기 접기(`indentRangeProvider.ts` `computeRanges`)와 같은 셈입니다.
 * @details 접기 범위 제공자를 하나라도 등록하면 VS Code 는 그 언어의 들여쓰기 접기를 쓰지 않습니다(제공자가 배열을 돌려주는 한).
 *          C/C++ · clangd 확장이 없는 편집기에서 이 확장이 주석 범위만 내면 코드 블록이 접히지 않게 되므로, 그때는 이 범위를 함께 냅니다.
 *          C · C++ 내장 언어 설정의 접기 표지(`#pragma region` / `#pragma endregion`)도 같은 규칙으로 셉니다.
 */

const kRegionStartPattern = /^\s*#pragma\s+region\b/;
const kRegionEndPattern = /^\s*#pragma\s+endregion\b/;
/** @brief 표지 영역을 스택에 표시하는 들여쓰기 값입니다(VS Code 와 같은 -2). */
const kMarkerIndent = -2;

/** @brief 줄의 보이는 들여쓰기 폭입니다. 빈 줄(공백만)은 -1 입니다. */
function computeIndentLevel(lineText, tabSize) {
    let indent = 0;
    for (let index = 0; index < lineText.length; ++index) {
        const character = lineText[index];
        if (character === ' ')
            ++indent;
        else if (character === '\t')
            indent = indent - (indent % tabSize) + tabSize;
        else if (character !== '\r')
            return indent;
    }
    return -1;
}

/**
 * @brief 들여쓰기 접기 범위를 구합니다.
 * @return `[{ start, end }]`(0 부터, start 순).
 */
function collectIndentRanges(text, tabSize) {
    if (!Number.isInteger(tabSize) || tabSize <= 0)
        throw new Error('collectIndentRanges: tabSize must be a positive integer');
    const listLine = text.split('\n');
    const lineCount = listLine.length;
    const listRange = [];
    // 아래에서 위로 훑는다. 줄 번호는 VS Code 와 같게 1 부터 세고 낼 때 0 부터로 바꾼다.
    const listRegion = [{ indent: -1, endAbove: lineCount + 1, line: lineCount + 1 }];
    for (let line = lineCount; line > 0; --line) {
        const lineText = listLine[line - 1];
        const indent = computeIndentLevel(lineText, tabSize);
        let previous = listRegion[listRegion.length - 1];
        if (indent === -1)
            continue;
        if (kRegionEndPattern.test(lineText)) {
            listRegion.push({ indent: kMarkerIndent, endAbove: line, line });
            continue;
        }
        if (kRegionStartPattern.test(lineText)) {
            let markerIndex = listRegion.length - 1;
            while (markerIndex > 0 && listRegion[markerIndex].indent !== kMarkerIndent)
                --markerIndex;
            if (markerIndex > 0) {
                listRegion.length = markerIndex + 1;
                previous = listRegion[markerIndex];
                listRange.push({ start: line - 1, end: previous.line - 1 });
                previous.line = line;
                previous.indent = indent;
                previous.endAbove = line;
                continue;
            }
        }
        if (previous.indent > indent) {
            do {
                listRegion.pop();
                previous = listRegion[listRegion.length - 1];
            } while (previous.indent > indent);
            const endLine = previous.endAbove - 1;
            if (endLine - line >= 1)
                listRange.push({ start: line - 1, end: endLine - 1 });
        }
        if (previous.indent === indent)
            previous.endAbove = line;
        else
            listRegion.push({ indent, endAbove: line, line });
    }
    return listRange.reverse();
}

module.exports = {
    collectIndentRanges,
};
