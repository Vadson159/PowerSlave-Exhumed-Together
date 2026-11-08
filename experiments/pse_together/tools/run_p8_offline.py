"""P8: complete offline regression in isolated short b68, serialized MSBuild.

Same run_offline implementation and suite summaries. Serial builds avoid DOC-67
F3's duplicate-project linker collisions. Existing b60 artifacts are untouched.
"""
import run_offline as runner

runner.BUILD = runner.ROOT / 'b68'
original_run = runner.run


def serial_run(argv, *args, **kwargs):
    argv = list(argv)
    if '--parallel' in argv:
        argv[argv.index('--parallel') + 1] = '1'
    return original_run(argv, *args, **kwargs)


if __name__ == '__main__':
    runner.run = serial_run
    raise SystemExit(runner.main())
