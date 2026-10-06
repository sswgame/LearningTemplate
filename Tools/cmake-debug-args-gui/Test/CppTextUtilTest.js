'use strict';

/**
 * @file CppTextUtilTest.js
 * @brief `CppTextUtil` 의 주석 지우기 · 매크로 인자 나누기 · 리터럴 읽기 시험입니다.
 */

const assert = require('node:assert/strict');
const test = require('node:test');

const CppTextUtil = require('../Source/CppTextUtil');

test('blankComments keeps strings, positions and line numbers', () => {
    const text = 'a // x "y"\nb /* c\n d */ "e // f" \'/\' R"(g // h)" 1\'000 // i\n';
    const blanked = CppTextUtil.blankComments(text);
    assert.equal(blanked.length, text.length);
    assert.equal(blanked.split('\n').length, text.split('\n').length);
    assert.ok(blanked.includes('"e // f"'));
    assert.ok(blanked.includes('R"(g // h)"'));
    assert.ok(blanked.includes('\'/\''));
    assert.ok(blanked.includes('1\'000'));
    assert.equal(blanked.includes('x "y"'), false);
    assert.equal(blanked.includes('d */'), false);
    assert.equal(blanked.includes('// i'), false);
});

test('blankComments does not treat a URL inside a string as a comment', () => {
    const text = 'SW_GLOBAL_VARIABLE( sw::string, gv_url, "https://example.com", "d" );';
    assert.equal(CppTextUtil.blankComments(text), text);
});

test('splitMacroArguments splits on top-level commas only', () => {
    const text = 'M( int32, gv_x, foo( 1, 2 ), "a, (b)", { 3, 4 } )';
    const result = CppTextUtil.splitMacroArguments(text, text.indexOf('('));
    assert.deepEqual(result.listArgument, ['int32', 'gv_x', 'foo( 1, 2 )', '"a, (b)"', '{ 3, 4 }']);
    assert.equal(result.closeIndex, text.length - 1);
    assert.equal(CppTextUtil.splitMacroArguments('M( a, b', 1), null);
});

test('findMacroCalls skips preprocessor directives and their continuation lines', () => {
    const text = [
        '#define SW_GLOBAL_VARIABLE( type, name, defaultValue, desc ) \\',
        '    SW_GLOBAL_VARIABLE( type, name, defaultValue, desc )',
        '// SW_GLOBAL_VARIABLE( int32, gv_comment, 0, "c" );',
        'SW_GLOBAL_VARIABLE( bool, gv_multi, false,',
        '                    "first "',
        '                    "second" );',
    ].join('\r\n');
    const listCall = CppTextUtil.findMacroCalls(CppTextUtil.blankComments(text), /\b(SW_GLOBAL_VARIABLE)\s*\(/g);
    assert.equal(listCall.length, 1);
    assert.equal(listCall[0].lineNumber, 4);
    assert.equal(listCall[0].listArgument[1], 'gv_multi');
    assert.equal(CppTextUtil.parseStringLiteralSequence(listCall[0].listArgument[3]), 'first second');
});

test('parseLiteral reads each literal kind and keeps expressions verbatim', () => {
    assert.deepEqual(CppTextUtil.parseLiteral('true'), { kind: 'bool', value: 'true' });
    assert.deepEqual(CppTextUtil.parseLiteral(' -12u '), { kind: 'int', value: '-12' });
    assert.deepEqual(CppTextUtil.parseLiteral('0.5f'), { kind: 'float', value: '0.5' });
    assert.deepEqual(CppTextUtil.parseLiteral('1e3'), { kind: 'float', value: '1e3' });
    assert.deepEqual(CppTextUtil.parseLiteral('u8"a\\"b\\n"'), { kind: 'string', value: 'a"b\n' });
    assert.deepEqual(CppTextUtil.parseLiteral('RHIBackend::DirectX12'), { kind: 'expression', value: 'RHIBackend::DirectX12' });
});

test('parseStringLiteralSequence rejects anything that is not only literals', () => {
    assert.equal(CppTextUtil.parseStringLiteralSequence('"a" + b'), null);
    assert.equal(CppTextUtil.parseStringLiteralSequence('kName'), null);
    assert.equal(CppTextUtil.parseStringLiteralSequence('"\\x41\\u00e9"'), 'Aé');
});

test('normalizeTypeName drops spaces and a leading ::, but keeps namespaces', () => {
    assert.equal(CppTextUtil.normalizeTypeName(' sw::string '), 'sw::string');
    assert.equal(CppTextUtil.normalizeTypeName('::std:: string'), 'std::string');
    assert.equal(CppTextUtil.normalizeTypeName('RHIBackend'), 'RHIBackend');
});
