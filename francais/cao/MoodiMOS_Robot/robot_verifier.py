"""Contrôle des STL : fermeture, volume, nombre de pièces et pose sur plateau.
Lancer après les générateurs avec le Python de MoodiMOS_Organique.
Un maillage valide ne certifie ni les collisions internes, ni les jeux réels.
"""
import json
import struct
from pathlib import Path
import numpy as np
from manifold3d import Manifold, Mesh

HERE = Path(__file__).resolve().parent


def inspect(path):
    data = path.read_bytes()
    if len(data) < 84:
        raise ValueError('STL incomplet')
    n = struct.unpack('<I', data[80:84])[0]
    if len(data) != 84 + 50 * n or n == 0:
        raise ValueError('STL vide ou longueur invalide')
    records = np.frombuffer(data, offset=84, count=n,
                           dtype=[('normal', '<f4', 3), ('v', '<f4', (3, 3)), ('attr', '<u2')])
    vertices, index = np.unique(records['v'].reshape(-1, 3), axis=0, return_inverse=True)
    m = Manifold(Mesh(vert_properties=vertices, tri_verts=index.reshape(-1, 3).astype(np.uint32)))
    lo, hi = vertices.min(0), vertices.max(0)
    return dict(status=m.status().name, components=len(m.decompose()) if not m.is_empty() else 0,
                volume_cm3=float(m.volume() / 1000), dimensions_mm=(hi - lo).tolist(),
                min_z_mm=float(lo[2]), closed=m.status().name == 'NoError' and m.volume() > 0)


def main():
    report = {'meshes': {}, 'failures': [], 'scope': 'Fermeture et pose des pièces imprimables ; pas de certification des composants.'}
    sup_path = HERE / 'stl' / 'supports_impression.json'      # corps détachables des STL d'impression (robot_pieces)
    supports = json.loads(sup_path.read_text()) if sup_path.exists() else {}
    for path in sorted((HERE / 'stl_impression').glob('*.stl')):
        key = path.name
        try:
            info = inspect(path)
            corps = 1 + supports.get(path.stem, {}).get('corps', 0)
            info['ok'] = info['closed'] and info['components'] == corps and abs(info['min_z_mm']) < 0.01
        except Exception as err:
            info = dict(ok=False, error=str(err))
        report['meshes'][key] = info
        if not info['ok']:
            report['failures'].append(key)
        print(('OK  ' if info['ok'] else 'ÉCHEC  ') + key + ' ' + json.dumps(info, ensure_ascii=False))
    import robot_pieces as robot
    source = robot.read_assign(str(HERE.parent / 'MoodiMOS_Torse' / 'MoodiMOS_Torse.py'), 'PARAMS')
    from robot_style import TORSO_STYLE
    protected = sorted(set(source) - set(TORSO_STYLE))
    altered = [key for key in protected if robot.TP[key] != source[key]]
    report['component_parameters'] = dict(ok=not altered, changed=altered,
                                         preserved=protected, design_overrides=TORSO_STYLE)
    if altered:
        report['failures'].append('Paramètres composants : ' + ', '.join(altered))
    report['count'] = len(report['meshes'])
    report['ok'] = bool(report['count']) and not report['failures']
    (HERE / 'rendus').mkdir(exist_ok=True)
    (HERE / 'rendus' / 'verification_maillages.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
    if not report['ok']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
