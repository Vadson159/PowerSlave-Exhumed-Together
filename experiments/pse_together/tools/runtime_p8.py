"""P8 binding of the accepted P1 launcher/isolation/DOC-51 loader.

Only run identities, paths, artifacts and the engine's standard -file argument
are changed. No new loader or injector. Run launch/watch through REA spawn.
"""
from pathlib import Path

_source = Path(__file__).with_name('runtime_p1.py').read_text(encoding='utf8')
_source = _source.replace('runtime_p1', 'runtime_p8').replace("runtime'", "runtime_p8'")
_source = _source.replace('PSE_TOGETHER_P1', 'PSE_TOGETHER_P8')
_source = _source.replace('P1_HOST_03', 'P8_HOST_04').replace('P1_CLIENT_03', 'P8_CLIENT_04')
_source = _source.replace('PRE_NATIVE_GATE_RUN03.json', 'PRE_NATIVE_GATE_RUN04.json')
_source = _source.replace('b60/dll/pse_dll_host/', 'b68/dll/pse_dll_host_p8/')
_source = _source.replace('b60/dll/pse_dll_client_proxy/', 'b68/dll/pse_dll_client_p8/')
# Execute the same helper definitions, retaining our own main below.
exec(compile(_source[:_source.rindex("if __name__ == '__main__':")], str(Path(__file__)), 'exec'))

_accepted_runner = accepted_runner


def accepted_runner(role):
    m = _accepted_runner(role)
    import subprocess
    original = subprocess.Popen
    expected = (m.prepare.COPY / 'anubis_Shipping_Steam_x64.exe').resolve()
    package = ROOT / 'experiments/pse_together/assets/coop_sprites_john.kpf'

    def with_assets(argv, *args, **kwargs):
        if isinstance(argv, list) and Path(argv[0]).resolve() == expected:
            # Runtime 01: KEX unconditionally prefixes basepath, even for an
            # absolute -file argument. Install only an additive file in the
            # verified workspace copy, and use the accepted relative namespace.
            dest = m.prepare.COPY / 'mods/coop_sprites_john.kpf'
            if dest.exists() and sha(dest)!=sha(package):
                raise ValueError('Refusing to replace an unrelated test-copy mod')
            dest.parent.mkdir(exist_ok=True)
            if not dest.exists():
                import shutil
                shutil.copy2(package,dest)
            # Runs 02/03 proved that KEX also auto-mounts mods/*.kpf. Run04
            # uses that native path alone, avoiding duplicate -file mounting.
        return original(argv, *args, **kwargs)

    subprocess.Popen = with_assets
    return m


if __name__ == '__main__':
    main()
