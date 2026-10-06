"""Development-only JSON-to-C++ data compiler. Not a runtime dependency.

The generated C++ is checked in and normal MSVC builds do not need Python.
This compiles literal data only, never JavaScript or any executable payload.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def cpp(v):
    if v is None:
        return 'Config(nullptr)'
    if isinstance(v, bool):
        return 'Config(' + str(v).lower() + ')'
    if isinstance(v, (int, float)):
        return 'Config(' + repr(float(v)) + ')'
    if isinstance(v, str):
        return 'Config(' + json.dumps(v, ensure_ascii=True) + ')'
    if isinstance(v, list):
        return 'Config(Config::Array{' + ','.join(map(cpp, v)) + '})'
    if isinstance(v, dict):
        return 'Config(Config::Object{' + ',\n'.join('{' + json.dumps(k) + ',' + cpp(x) + '}' for k, x in v.items()) + '})'
    raise TypeError(type(v))

def main():
    profiles = json.loads((ROOT / 'profiles/accepted-0930.json').read_text(encoding='utf-8'))
    policy = json.loads((ROOT / 'profiles/entry-policy.json').read_text(encoding='utf-8'))
    parts = ['// Generated literal data. See tools/compile_profile.py.\n#include "config.hpp"\nnamespace csnz {']
    # Split factories to keep MSVC's initializer/function size bounded.
    for name, data in profiles.items():
        parts.append(f'static Config make_{name}(){{return {cpp(data)};}}')
    factories = ','.join('{' + json.dumps(k) + f',make_{k}()' + '}' for k in profiles)
    parts.append('const Config& profiles(){static const Config p(Config::Object{' + factories + '});return p;}')
    parts.append('const Config& entryPolicy(){static const Config p=' + cpp(policy) + ';return p;}\n}')
    (ROOT / 'shared/src/profile_data.cpp').write_text('\n'.join(parts) + '\n', encoding='utf-8')

if __name__ == '__main__':
    main()
