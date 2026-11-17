#!/usr/bin/env python3
"""Chapter 25 Challenge 2 loop iteration and continue regression tests."""
from pathlib import Path
import os
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
EXEC = Path(os.environ.get('CLOX_BIN', str(ROOT / 'clox')))

SUCCESS = {
  'independent captures for each iteration': (
    'var a;var b;var c;for(var i=0;i<3;i=i+1){'
    'fun read(){return i;}if(i==0)a=read;else if(i==1)b=read;else c=read;}'
    'print a();print b();print c();', '0\n1\n2\n'),
  'body assignment affects next iteration with no increment': (
    'var a;var b;for(var i=0;i<2;){fun read(){return i;}'
    'if(i==0)a=read;else b=read;i=i+1;}'
    'print a();print b();', '1\n2\n'),
  'continue closes and persists its own iteration value': (
    'var a;var b;var c;for(var i=0;i<3;i=i+1){'
    'fun read(){return i;}if(i==0){a=read;continue;}'
    'if(i==1){b=read;continue;}c=read;}'
    'print a();print b();print c();', '0\n1\n2\n'),
  'continue copies back iteration variable with no increment clause': (
    'var total=0;for(var i=0;i<4;){i=i+1;if(i==2)continue;total=total+i;}'
    'print total;', '8\n'),
  'continue closes captured inner block locals': (
    'var f;for(var i=0;i<3;i=i+1){{var x=i*10;fun read(){return x;}'
    'if(i==1){f=read;continue;}}}print f();', '10\n'),
  'nested while continue does not skip outer loop': (
    'var sum=0;for(var i=0;i<3;i=i+1){var j=0;'
    'while(j<3){j=j+1;if(j==1)continue;sum=sum+j;}}'
    'print sum;', '15\n'),
  'nested for continue targets inner loop': (
    'var sum=0;for(var i=0;i<3;i=i+1){'
    'for(var j=0;j<3;j=j+1){if(j==1)continue;sum=sum+i*10+j;}}'
    'print sum;', '66\n'),
  'outer continue after inner loop': (
    'var sum=0;for(var i=0;i<4;i=i+1){'
    'for(var j=0;j<2;j=j+1){if(j==0)continue;sum=sum+1;}'
    'if(i==1)continue;sum=sum+10;}print sum;', '34\n'),
  'for without declaration retains existing capture semantics': (
    'var i=0;var first;for(i=0;i<3;i=i+1){'
    'fun read(){return i;}if(i==0)first=read;}print first();', '3\n'),
  'captured const for initializer can be read': (
    'fun make(){for(const i=5;i<6;){fun read(){return i;}return read;}'
    'return nil;}print make()();', '5\n'),
  'captured loop local beyond 255': (
    'fun outer(){'+''.join('var v'+str(j)+';' for j in range(300))+
    'var f;for(var i=0;i<2;i=i+1){fun read(){return i;}'
    'if(i==0)f=read;}return f;}print outer()();','0\n'),
  'nested function own loop context is independent': (
    'for(var i=0;i<2;i=i+1){fun maker(){var n=0;'
    'for(var j=0;j<3;j=j+1){if(j==1)continue;n=n+j;}return n;}'
    'print maker();}', '2\n2\n'),
  'return closes an iteration capture': (
    'fun make(){for(var i=0;i<4;i=i+1){'
    'fun read(){return i;}if(i==2)return read;}return nil;}'
    'print make()();', '2\n'),
}

ERRORS = {
  'cannot change a const for initializer': (
    'for(const i=0;i<2;){i=i+1;}', "Can't assign to a constant variable."),
  'nested function cannot continue its caller loop': (
    'for(var i=0;i<2;i=i+1){fun f(){continue;}f();}',
    "Can't use 'continue' outside of a loop."),
}

def main():
    if not EXEC.is_file():
        print('Please build clox first: gcc -std=c99 src/*.c -o clox')
        return 1
    with tempfile.TemporaryDirectory(prefix='clox_ch25_ch2_') as folder:
        path = Path(folder) / 'test.lox'
        for name, (source, expected) in SUCCESS.items():
            path.write_text(source)
            try:
                result = subprocess.run([str(EXEC), str(path)], capture_output=True,
                                        text=True, timeout=8)
            except subprocess.TimeoutExpired:
                print('FAIL — '+name+' timed out'); return 1
            good = result.returncode == 0 and result.stdout == expected
            print(('PASS' if good else 'FAIL')+' — '+name)
            if not good:
                print('Expected:',repr(expected),'Actual:',repr(result.stdout),'Error:',repr(result.stderr));return 1
        for name, (source, expected) in ERRORS.items():
            path.write_text(source)
            result = subprocess.run([str(EXEC), str(path)], capture_output=True,
                                    text=True, timeout=8)
            good = result.returncode != 0 and expected in result.stderr
            print(('PASS' if good else 'FAIL')+' — '+name)
            if not good:
                print('Expected error:',expected,'Actual:',repr(result.stdout),repr(result.stderr));return 1
    print(f'ALL {len(SUCCESS)+len(ERRORS)} CHALLENGE 2 TESTS PASSED')
    return 0

if __name__=='__main__': sys.exit(main())
