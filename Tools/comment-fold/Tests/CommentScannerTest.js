'use strict';

/**
 * @file CommentScannerTest.js
 * @brief `CommentScanner` 의 토크나이저(문자열 · 원시 문자열 · 줄 잇기 · 자릿수 구분자 · CRLF)와 접을 범위 고르기 테스트입니다.
 */

const assert = require('node:assert/strict');
const test = require('node:test');

const { scanComments, collectFoldRanges } = require('../Source/CommentScanner');

const kDefaultOptions = { minimumLineCount: 2, excludeFileHeader: false };

/** @brief 글의 접을 범위를 `[start, end]` 쌍으로 돌려줍니다. */
function foldRanges(listLine, options = kDefaultOptions, newLine = '\n') {
    return collectFoldRanges(scanComments(listLine.join(newLine)), options).map((range) => [range.start, range.end]);
}

test('comment markers inside string and character literals are not comments', () => {
    const { listComment } = scanComments('const char* a = "// x /* y"; char b = \'/\'; const char* c = "\\" // still string"; // real');
    assert.equal(listComment.length, 1);
    assert.equal(listComment[0].text, '// real');
    assert.equal(listComment[0].isOwnLine, false);
});

test('raw strings hide comment markers, also across lines and with a delimiter', () => {
    const text = [
        'auto a = R"(// not )" /* nor */ ")";',
        'auto b = u8R"xy(line',
        '/* still raw',
        ')" // still raw )xy";',
        '// comment on line 4',
    ].join('\n');
    const { listComment } = scanComments(text);
    assert.deepEqual(listComment.map((comment) => [comment.startLine, comment.text]), [[0, '/* nor */'], [4, '// comment on line 4']]);
});

test('digit separators are not character literals', () => {
    const { listComment } = scanComments("int a = 1'000'000; // one\nint b = 0x1'F; float c = 1.5e+3f; // two\n");
    assert.deepEqual(listComment.map((comment) => comment.text), ['// one', '// two']);
});

test('a backslash at the end of a // comment continues it onto the next line', () => {
    const { listComment } = scanComments('// first \\\n   second\nint x;\n');
    assert.equal(listComment.length, 1);
    assert.equal(listComment[0].startLine, 0);
    assert.equal(listComment[0].endLine, 1);
});

test('a backslash continues a string onto the next line', () => {
    const { listComment } = scanComments('const char* s = "a \\\n// inside";\n// outside\n');
    assert.deepEqual(listComment.map((comment) => [comment.startLine, comment.text]), [[2, '// outside']]);
});

test('block comments do not nest: /* /* */ ends at the first */', () => {
    const { listComment } = scanComments('/* /* */ int x; */\n');
    assert.equal(listComment.length, 1);
    assert.equal(listComment[0].text, '/* /* */');
    assert.equal(listComment[0].hasCodeAfter, true);
});

test('an unterminated block comment runs to the end of the text', () => {
    const { listComment } = scanComments('int x;\n/* open\nnever closed');
    assert.equal(listComment.length, 1);
    assert.equal(listComment[0].endLine, 2);
});

test('doc comment markers follow Doxygen', () => {
    const { listComment } = scanComments('/// a\n//! b\n//// c\n// d\n/** e */\n/*! f */\n/**/\n/* g */\n');
    assert.deepEqual(listComment.map((comment) => comment.isDoc), [true, true, false, false, true, true, false, false]);
});

test('a run of // lines folds as one range; a blank line or code ends the run', () => {
    assert.deepEqual(foldRanges([
        '// a',     // 0
        '// b',     // 1
        '',         // 2
        '// c',     // 3
        '// d',     // 4
        'int x;',   // 5
        '// e',     // 6
        '// f',     // 7
        '// g',     // 8
    ]), [[0, 1], [3, 4], [6, 8]]);
});

test('a doc run and a plain run next to each other fold separately', () => {
    assert.deepEqual(foldRanges(['/// a', '/// b', '// c', '// d', '//! e', '/// f']), [[0, 1], [2, 3], [4, 5]]);
});

test('end-of-line comments never fold and break a run', () => {
    assert.deepEqual(foldRanges([
        'int a; // x',  // 0
        'int b; // y',  // 1
        '// c',         // 2
        'int d; // z',  // 3
        '// e',         // 4
    ]), []);
    assert.deepEqual(foldRanges(['void f() { /* a', '   b */', '}']), [], 'a block comment after code on its first line does not fold');
});

test('a single-line comment does not fold; minimumLineCount raises the bar', () => {
    assert.deepEqual(foldRanges(['// a', 'int x;', '/* b */']), []);
    assert.deepEqual(foldRanges(['/* a', ' b */', '// c', '// d', '// e'], { minimumLineCount: 3, excludeFileHeader: false }), [[2, 4]]);
});

test('code after */ on the last line keeps that line visible', () => {
    assert.deepEqual(foldRanges(['/* a', ' b', ' c */ int x;']), [[0, 1]]);
    assert.deepEqual(foldRanges(['/* a', ' b */ int x;']), [], 'only one comment line would be left');
});

test('CRLF text gives the same ranges as LF', () => {
    const listLine = ['/**', ' * doc', ' */', 'int x; // t', '// a', '// b \\', '   c', 'const char* s = "// q";'];
    assert.deepEqual(foldRanges(listLine, kDefaultOptions, '\r\n'), foldRanges(listLine));
    assert.deepEqual(foldRanges(listLine), [[0, 2], [4, 6]]);
});

test('excludeFileHeader keeps leading comments and @file blocks open', () => {
    const listLine = [
        '// Copyright line 1',      // 0
        '// Copyright line 2',      // 1
        '#pragma once',             // 2
        '/**',                      // 3
        ' * @file Sample.h',        // 4
        ' */',                      // 5
        '#include <a>',             // 6
        '/** class doc',            // 7
        ' */',                      // 8
        'class A {};',              // 9
        '/* \\file also a header */', // 10
    ];
    assert.deepEqual(foldRanges(listLine), [[0, 1], [3, 5], [7, 8]]);
    assert.deepEqual(foldRanges(listLine, { minimumLineCount: 2, excludeFileHeader: true }), [[7, 8]]);
});

test('collectFoldRanges requires every option', () => {
    assert.throws(() => collectFoldRanges(scanComments(''), { minimumLineCount: 2 }), /required/);
});

test('a large file scans quickly', () => {
    const listLine = [];
    for (let index = 0; index < 20000; ++index)
        listLine.push(index % 5 === 0 ? '' : index % 2 === 0 ? `// comment ${index} "quoted"` : `int value${index} = ${index}'000; /* note */ const char* s = "/* x */";`);
    const startTime = process.hrtime.bigint();
    const listRange = foldRanges(listLine);
    const elapsedMs = Number(process.hrtime.bigint() - startTime) / 1e6;
    assert.equal(listRange.length, 0, 'comment lines alternate with code, so no run reaches two lines');
    assert.ok(elapsedMs < 500, `20 000 lines took ${elapsedMs} ms`);
});
