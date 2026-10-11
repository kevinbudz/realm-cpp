#!/usr/bin/env python3
"""Compile test exporters against the authoritative, unmodified Flash source."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

def build(program, flash_root, sdk, java_home, output, mask_variant=None):
    output.mkdir(parents=True, exist_ok=True)
    asset_loader = (flash_root / 'src/com/company/assembleegameclient/util/AssetLoader.as').read_text()
    asset_loader = asset_loader.replace('AssetLoader', 'ReferenceAssets')
    for call in ('this.addSoundEffects();', 'Parameters.load();', 'Options.refreshCursor();'):
        asset_loader = asset_loader.replace(call, '')
    generated = output / 'com/company/assembleegameclient/util/ReferenceAssets.as'
    generated.parent.mkdir(parents=True, exist_ok=True); generated.write_text(asset_loader)
    image_set_path = output / 'com/company/util/ImageSet.as'
    if mask_variant is not None:
        image_set = (flash_root / 'src/com/company/util/ImageSet.as').read_text()
        image_set = image_set.replace('int(Math.random() * this.images_.length)', f'{mask_variant} % this.images_.length')
        image_set_path.parent.mkdir(parents=True, exist_ok=True); image_set_path.write_text(image_set)
    elif image_set_path.exists():
        image_set_path.unlink()
    env = os.environ.copy(); env['JAVA_HOME'] = str(java_home); env['PATH'] = str(java_home / 'bin') + os.pathsep + env['PATH']
    source = Path(__file__).resolve().parent / 'flash-reference'
    command = [str(sdk / 'bin/mxmlc'), '+configname=air', '-source-path+=' + str(output), '-source-path+=' + str(flash_root / 'src'), '-output', str(output / (program + '.swf')), '-optimize=true', '-strict=true', '-keep-as3-metadata+=Inject', '-keep-as3-metadata+=PostConstruct', '-keep-as3-metadata+=ArrayElementType']
    command += ['-library-path+=' + str(x) for x in sorted((flash_root / 'libs').glob('*.swc'))]
    command += ['--', str(source / (program + '.as'))]
    subprocess.run(command, env=env, check=True)
    # Use this SDK's descriptor namespace, matching its default SWF version.
    namespace = ET.parse(sdk / 'airsdk.xml').getroot().tag.split('}')[0][1:].replace('/sdk/', '/application/')
    descriptor = (source / 'TextureExporter-app.xml').read_text().replace('TextureExporter', program).replace('realm.cpp.reference.textures', 'realm.cpp.reference.' + program.lower())
    # AIR forwards invokes to an existing process with the same application ID.
    # Separate reference directories must be able to run independently.
    suffix = hashlib.sha256(str(output.resolve()).encode()).hexdigest()[:12]
    descriptor = descriptor.replace('</id>', '.p' + suffix + '</id>')
    import re
    descriptor = re.sub(r'http://ns.adobe.com/air/application/[0-9.]+', namespace, descriptor)
    if program == 'StageTextureExporter': descriptor = descriptor.replace('<renderMode>cpu</renderMode>', '<renderMode>direct</renderMode>')
    (output / (program + '-app.xml')).write_text(descriptor)
    return output / (program + '-app.xml')

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', choices=['TextureExporter', 'GroundExporter', 'BlendExporter', 'StageTextureExporter', 'FontExporter', 'HudExporter', 'MinimapExporter', 'BitmapColorExporter', 'InventoryExporter'])
    p.add_argument('--flash-root', type=Path, default=Path(__file__).resolve().parents[2] / 'realm-client')
    p.add_argument('--sdk', type=Path, default=Path(os.environ.get('AIR_SDK', str(Path.home() / 'Desktop/AIRSDK_Linux'))))
    p.add_argument('--java-home', type=Path, default=Path(os.environ.get('JAVA_HOME', str(Path.home() / '.local/share/air-jdk'))))
    p.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'build/flash-reference')
    p.add_argument('--mask-variant', type=int, choices=[0, 1, 2], help='Control only randomized ImageSet choices in the isolated test build')
    a = p.parse_args()
    build(a.program, a.flash_root.resolve(), a.sdk.resolve(), a.java_home.resolve(), a.output.resolve(), a.mask_variant)
