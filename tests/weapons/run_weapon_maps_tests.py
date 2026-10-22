"""Audit extracted weapon prediction maps with SDK datamap descriptors.

Minimal layout wrappers (real SDK trace/handle types for knife), not actual weapon instances/CPredictionCopy.
Tests descriptor types, duplicate names, network flags, and typed roundtrips; engine prediction copying is not exercised.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

# State required by the current weapon command paths. Unchanged existing fields
# are controls too: DEFINE_FIELD already participates in prediction copy.
REQUIRED = {
    'CWeaponCSBase': {'m_flDecreaseShotsFired': ('float', False), 'm_fAccuracyPenalty': ('float', False)},
    'CWeaponCSBaseGun': {'m_zoomFullyActiveTime': ('float', False)},
    'CWeaponGlock': {'m_bBurstMode': ('bool', True), 'm_iGlock18ShotsFired': ('int', False),
                    'm_flGlock18Shoot': ('float', False), 'm_flLastFire': ('float', False)},
    'CWeaponUSP': {'m_bSilencerOn': ('bool', True), 'm_flDoneSwitchingSilencer': ('float', True),
                  'm_flLastFire': ('float', False)},
    'CWeaponFamas': {'m_bBurstMode': ('bool', True), 'm_flFamasShoot': ('float', False),
                    'm_iFamasShotsFired': ('int', False), 'm_fBurstSpread': ('float', False)},
    'CWeaponM3': {'m_flPumpTime': ('float', False), 'm_fInSpecialReload': ('int', True)},
    'CWeaponXM1014': {'m_flPumpTime': ('float', False), 'm_fInSpecialReload': ('int', True)},
    'CBaseCSGrenade': {'m_bRedraw': ('bool', True), 'm_bPinPulled': ('bool', True), 'm_fThrowTime': ('float', True)},
    'CC4': {'m_bStartedArming': ('bool', True), 'm_bBombPlacedAnimation': ('bool', True),
            'm_fArmedTime': ('float', True), 'm_bBombPlanted': ('bool', False)},
    'CDEagle': {'m_flLastFire': ('float', False)},
    'CWeaponElite': {'m_flLastFire': ('float', False)},
    'CWeaponFiveSeven': {'m_flLastFire': ('float', False)},
    'CWeaponP228': {'m_flLastFire': ('float', False)},
    'CWeaponG3SG1': {'m_flLastFire': ('float', False)},
    'CWeaponSG550': {'m_flLastFire': ('float', False)},
    'CWeaponM4A1': {'m_bSilencerOn': ('bool', True), 'm_flDoneSwitchingSilencer': ('float', True)},
    'CKnife': {'m_flSmackTime': ('float', True), 'm_bStab': ('bool', False),
               'm_pTraceHitEnt': ('EHANDLE', False), 'm_trHit.startpos': ('Vector', False),
               'm_trHit.endpos': ('Vector', False), 'm_trHit.fraction': ('float', False),
               'm_trHit.surface.flags': ('unsigned short', False),
               'm_trHit.surface.surfaceProps': ('short', False), 'm_trHit.hitbox': ('int', False)},
}
TYPE = {'float': 'FIELD_FLOAT', 'bool': 'FIELD_BOOLEAN', 'int': 'FIELD_INTEGER', 'Vector': 'FIELD_VECTOR',
        'short': 'FIELD_SHORT', 'unsigned short': 'FIELD_SHORT', 'EHANDLE': 'FIELD_EHANDLE'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--expect-defect', action='store_true')
    args = parser.parse_args()
    root = args.source_root or Path(__file__).resolve().parents[2]
    sources = root / 'game/shared/cstrike'
    generated, inventory, issues, seen = [], [], [], set()
    for p in sorted(sources.glob('weapon_*.cpp')):
        text = p.read_bytes().decode('latin1')
        declarations = text + ((p.with_suffix('.h').read_bytes().decode('latin1')) if p.with_suffix('.h').exists() else '')
        for cls, body in re.findall(r'BEGIN_PREDICTION_DATA\s*\(\s*(\w+)\s*\)(.*?)END_PREDICTION_DATA\s*\(\s*\)', text, re.S):
            seen.add(cls)
            # Client preprocessor guards only; nested trace fields are audited as well.
            body = re.sub(r'^\s*#.*$', '', body, flags=re.M)
            fields = list(re.finditer(r'DEFINE_(PRED_FIELD_TOL|PRED_FIELD|FIELD)\s*\(\s*([\w.]+)\s*,\s*(\w+)([^)]*)\)', body))
            actual = {}; members = {}
            for f in fields:
                name, fieldtype, tail = f.group(2, 3, 4)
                if name in actual: issues.append(f'{cls}: duplicate {name}')
                actual[name] = (fieldtype, 'FTYPEDESC_INSENDTABLE' in tail)
                network = re.search(r'CNetworkVar\s*\(\s*(bool|float|int)\s*,\s*' + re.escape(name) + r'\s*\)', declarations)
                plain = re.search(r'\b(bool|float|int)\s+' + re.escape(name) + r'\s*[;=]', declarations)
                typ = network.group(1) if network else plain.group(1) if plain else None
                if cls == 'CKnife':
                    # Check non-scalar declarations against the SDK headers too.
                    sdk_types = {
                        'm_pTraceHitEnt': ('EHANDLE', sources / 'weapon_knife.h', r'EHANDLE\s+m_pTraceHitEnt\s*;'),
                        'm_trHit.startpos': ('Vector', root / 'public/trace.h', r'Vector\s+startpos\s*;'),
                        'm_trHit.endpos': ('Vector', root / 'public/trace.h', r'Vector\s+endpos\s*;'),
                        'm_trHit.fraction': ('float', root / 'public/trace.h', r'float\s+fraction\s*;'),
                        'm_trHit.surface.flags': ('unsigned short', root / 'public/cmodel.h', r'unsigned\s+short\s+flags\s*;'),
                        'm_trHit.surface.surfaceProps': ('short', root / 'public/cmodel.h', r'short\s+surfaceProps\s*;'),
                        'm_trHit.hitbox': ('int', root / 'public/gametrace.h', r'int\s+hitbox\s*;'),
                    }
                    if name in sdk_types:
                        typ, header, pattern = sdk_types[name]
                        assert re.search(pattern, header.read_bytes().decode('latin1')), name
                if typ is None:
                    # Inherited attack/idle timers explicitly mapped by the CS base.
                    if cls == 'CWeaponCSBase' and name in ('m_flTimeWeaponIdle', 'm_flNextPrimaryAttack', 'm_flNextSecondaryAttack'):
                        typ = 'float'
                    else: raise AssertionError(f'Unresolved real declaration: {cls}.{name}')
                members[name] = typ
                if fieldtype != TYPE[typ]: issues.append(f'{cls}: {name} declares {typ}, maps {fieldtype}')
            for name, (typ, network) in REQUIRED.get(cls, {}).items():
                if actual.get(name) != (TYPE[typ], network): issues.append(f'{cls}: missing/wrong required field {name}')
            # Compare INSENDTABLE flags with actual SendProp registrations, not CNetworkVar spelling.
            for name, (_, network) in actual.items():
                in_table = bool(re.search(r'SendProp\w+\s*\(\s*SENDINFO\s*\(\s*' + re.escape(name) + r'\s*\)', text))
                if network != in_table: issues.append(f'{cls}: wrong sendtable flag {name}')
            inventory.append({'file': p.name, 'class': cls, 'fields': list(actual)})
            if cls == 'CKnife':
                lines = ['struct Layout_CKnife { float m_flSmackTime; bool m_bStab;',
                         'EHANDLE m_pTraceHitEnt; trace_t m_trHit; };']
            else:
                lines = [f'struct Layout_{cls} {{'] + [f'    {typ} {name};' for name, typ in members.items()] + ['};']
            lines += [f'void test_{cls}() {{', f'    typedef Layout_{cls} classNameTypedef;', '    typedescription_t desc[] = {',
                      '        { FIELD_VOID, 0, {0,0}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f },', body, '    };',
                      f'    testDescriptors<Layout_{cls}>(desc, sizeof(desc)/sizeof(desc[0]));', '}']
            generated += lines
    if REQUIRED.keys() - seen: raise AssertionError('Missing maps: ' + str(REQUIRED.keys() - seen))
    print(f'Audited weapon files={len(list(sources.glob("weapon_*.cpp")))} maps={len(inventory)} issues={len(issues)}', flush=True)
    for issue in issues: print(issue, flush=True)
    if args.expect_defect:
        if not issues: raise AssertionError('Expected old map defects')
        print('BASELINE MAP DEFECTS REPRODUCED', flush=True)
        return
    if issues: raise AssertionError('Weapon prediction map audit failed')
    with tempfile.TemporaryDirectory(prefix='cssv34-weapon-maps-') as directory:
        out = Path(directory)
        (out / 'weapon_maps.inc').write_text('\n'.join(generated))
        (out / 'weapon_calls.inc').write_text('\n'.join(f'test_{x["class"]}();' for x in inventory))
        executable = out / 'weapon_maps_tests'
        command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-O2', '-g', '-Wall', '-Wextra',
                   '-Wno-unused-local-typedefs', '-Wno-unused-parameter', '-Wno-deprecated-copy',
                   '-DPOSIX', '-DLINUX', '-D_LINUX', '-DGNUC', '-DCOMPILER_GCC', '-DPLATFORM_64BITS',
                   '-DNO_MALLOC_OVERRIDE', '-DCLIENT_DLL', '-I', str(root / 'game/shared'), '-isystem', str(root / 'public/tier0'), '-isystem', str(root / 'public/tier1'), '-isystem', str(root / 'public'), '-I', str(out),
                   str(Path(__file__).with_name('weapon_maps_tests.cpp')), '-o', str(executable)]
        if args.debug: command += ['-O0', '-D_GLIBCXX_ASSERTIONS']
        if args.sanitize: command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie']
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=60)


if __name__ == '__main__':
    main()
