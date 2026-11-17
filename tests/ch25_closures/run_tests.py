#!/usr/bin/env python3
"""Standalone tests for clox Chapter 25 (Sections 25.2-25.4).

Run from your repository root:
    gcc -std=c99 src/*.c -o clox
    python3 tests/ch25_closures/run_tests.py
"""
from pathlib import Path
import subprocess
import os
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
EXEC = Path(os.environ.get('CLOX_BIN', str(ROOT / 'clox')))

SUCCESS = {
    'native functions and basic calls': (
        'fun add(a,b){return a+b;} print add(2,3); print square(4); print abs(-7);',
        '5\n16\n7\n'),
    'independent counters': (
        'fun makeCounter(){var x=0;fun count(){x=x+1;return x;}return count;}'
        'var a=makeCounter();print a();print a();var b=makeCounter();print b();print a();',
        '1\n2\n1\n3\n'),
    'capturing across three levels': (
        'fun outer(){var x="value";fun middle(){fun inner(){print x;}return inner;}'
        'return middle;}var m=outer();var i=m();i();',
        'value\n'),
    'sibling closures share one mutable variable': (
        'var set;var get;fun main(){var a="initial";fun setter(){a="updated";}'
        'fun getter(){return a;}set=setter;get=getter;}main();set();print get();',
        'updated\n'),
    'block-scoped value survives': (
        'fun test(){var f;{var x="block";fun inner(){return x;}f=inner;}'
        'var x="new";return f();}print test();',
        'block\n'),
    'for captures a distinct iteration variable after Challenge 2': (
        'var f;for(var i=0;i<3;i=i+1){fun get(){return i;}if(i==1)f=get;}print f();',
        '1\n'),
    'deep function calls keep closure alive': (
        'fun maker(z){var x=z;fun capture(){return x;}return capture;}'
        'var f=maker(21);fun dive(n){if(n==0)return f();return dive(n-1);}print dive(45);',
        '21\n'),
    'continue closes captured block local': (
        'var first;var second;var count=0;for(var i=0;i<3;i=i+1){{var x=i;'
        'fun inner(){return x;}if(i==0)first=inner;if(i==1)second=inner;}'
        'if(i==1)continue;count=count+1;}'
        'print first();print second();print count;',
        '0\n1\n2\n'),
    'read captured variable modified while still open': (
        'fun outer(){var x=1;fun inc(){x=x+2;}inc();print x;}outer();',
        '3\n'),
    'captured parameter persists': (
        'fun holder(a){fun inner(){a=a+1;return a;}return inner;}'
        'var x=holder(10);print x();print x();',
        '11\n12\n'),
    'transitive returned closure mutation': (
        'fun outer(){var x=1;fun middle(){fun inner(){x=x+1;return x;}'
        'return inner;}return middle;}var mid=outer();var inner=mid();'
        'print inner();print inner();',
        '2\n3\n'),
    'local shadows global': (
        'var x="g";fun outer(){var x="o";fun inner(){print x;}'
        'inner();}outer();print x;',
        'o\ng\n'),
    'closure callback': (
        'fun twice(callback){return callback()+callback();}'
        'fun setup(){var x=1;fun incr(){x=x+1;return x;}return incr;}'
        'print twice(setup());',
        '5\n'),
    'transitive captured const can be read': (
        'fun outer(){const x=42;fun middle(){fun inner(){return x;}'
        'return inner;}return middle;}print outer()()();',
        '42\n'),
    'while loop continue with captured local': (
        'var f;var i=0;while(i<3){var x=i;fun get(){return x;}'
        'if(i==1)f=get;i=i+1;continue;}print f();',
        '1\n'),
    'earlier switch behavior': (
        'switch(2){case 1:print "a";case 2:print "b";default:print "c";}',
        'b\n'),
    'captured local beyond one-byte index': (
        'fun outer(){'+''.join(f'var v{i};' for i in range(300))+
        'var x=77;fun get(){return x;}return get;}print outer()();',
        '77\n'),
    'stack relocation preserves open upvalue pointers': (
        'fun outer(){var x=42;fun get(){return x;}'+
        ''.join(f'var v{i};' for i in range(700))+
        'return get;}var saved=outer();print saved();',
        '42\n'),
    'multiple captured locals close in stack order': (
        'fun maker(){var x=1;var y=2;fun get(){return x*10+y;}'
        'return get;}print maker()();',
        '12\n'),
}

ERRORS = {
    'cannot change captured const': (
        'fun outer(){const x=1;fun inner(){x=2;}return inner;}print outer()();',
        "Can't assign to a constant variable."),
    'continue must not reach enclosing function loop': (
        'for(var i=0;i<2;i=i+1){fun nested(){continue;}nested();}',
        "Can't use 'continue' outside of a loop."),
}

def main():
    if not EXEC.is_file():
        print('ERROR: First run: gcc -std=c99 src/*.c -o clox')
        return 1
    with tempfile.TemporaryDirectory(prefix='clox_ch25_') as folder:
        folder = Path(folder)
        for name, (source, expected) in SUCCESS.items():
            filename = folder / 'test.lox'
            filename.write_text(source)
            completed = subprocess.run([str(EXEC), str(filename)],
                                       capture_output=True, text=True, timeout=15)
            ok = completed.returncode == 0 and completed.stdout == expected
            print(('PASS' if ok else 'FAIL') + ' — ' + name)
            if not ok:
                print(' Expected:', repr(expected))
                print(' Actual:', repr(completed.stdout))
                print(' Error:', repr(completed.stderr))
                return 1
        for name, (source, expected_error) in ERRORS.items():
            filename = folder / 'test.lox'
            filename.write_text(source)
            completed = subprocess.run([str(EXEC), str(filename)],
                                       capture_output=True, text=True, timeout=15)
            ok = completed.returncode != 0 and expected_error in completed.stderr
            print(('PASS' if ok else 'FAIL') + ' — ' + name)
            if not ok:
                print(' Expected error:', expected_error)
                print(' Actual:', repr(completed.stdout), repr(completed.stderr))
                return 1
    print(f'ALL {len(SUCCESS) + len(ERRORS)} CHAPTER 25 TESTS PASSED')
    return 0

if __name__ == '__main__':
    sys.exit(main())
