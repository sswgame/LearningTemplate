'use strict';

/**
 * @file IndentFoldingTest.js
 * @brief `IndentFolding` 이 VS Code 들여쓰기 접기와 같은 범위를 내는지 봅니다.
 */

const assert = require('node:assert/strict');
const test = require('node:test');

const { collectIndentRanges } = require('../Source/IndentFolding');

function indentRanges(listLine, tabSize = 4) {
    return collectIndentRanges(listLine.join('\n'), tabSize).map((range) => [range.start, range.end]);
}

test('a brace block folds from its header to the last indented line', () => {
    assert.deepEqual(indentRanges([
        'int f() {',        // 0
        '    if (x) {',     // 1
        '        y();',     // 2
        '',                 // 3
        '        z();',     // 4
        '    }',            // 5
        '}',                // 6
        'int g();',         // 7
    ]), [[0, 5], [1, 4]]);
});

test('tabs count to the next tab stop, CRLF is ignored', () => {
    assert.deepEqual(collectIndentRanges('a\r\n\tb\r\n    c\r\nd', 4).map((range) => [range.start, range.end]), [[0, 2]]);
});

test('#pragma region pairs fold like the built-in C++ markers', () => {
    assert.deepEqual(indentRanges(['#pragma region A', 'int a;', 'int b;', '#pragma endregion', 'int c;']), [[0, 3]]);
});

test('tabSize must be a positive integer', () => {
    assert.throws(() => collectIndentRanges('a', 0), /tabSize/);
});
