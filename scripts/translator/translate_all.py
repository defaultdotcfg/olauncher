"""translate_all.py <game_dir> [--version V] [--optimize]
Incremental shader pipeline used by the launcher (also packaged as olauncher-translator.exe):
  1. build_shader_cache : new dumps (shaders/pso_*.dxbc) -> shaders/translated/<hash>.hlsl
  2. tess_bypass        : new tessellation dumps (shaders/tess) -> tess_*.dxil.cso
  3. compile_cso        : .hlsl -> .cso / .dxil.cso (up-to-date files are skipped); compute shaders are
                         compiled without fxc optimizations (fxc takes up to a minute on some of them)
  4. --optimize         : recompiles those compute shaders with optimizations (the launcher asks for it
                         after the game closes, so it never delays starting the game)
Shaders that fail are listed in shaders/translated/failed.txt and not retried until the version changes.
Writes shaders/translated/skip.txt (compute kernels the bridge must leave untranslated) when missing.
Progress for the launcher on stdout: "PROGRESS <stage 1-4> <done> <total>", last line "DONE <new> <failed>".
Internal: translate_all --gcn2hlsl <src.dxbc> <out.hlsl> (used by tess_bypass inside the packaged exe)."""
import sys, os, json, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


# Bump TRANSLATOR_REV when a translator fix changes shaders that players already have translated, and list
# the kinds to redo for that revision ('vs', 'ps', 'cs', 'tess').
# 2: VS inputs typed from the signature (uint BLENDINDICES); unread VS attributes take no VGPRs (skinned
#    depth prepass/shadow VS -> invisible drivers); precise SV_Position.
# 3: descriptor/constant tracking restarts from the branch state on `else` sides and jump-only labels (TAA, volumetric
#    fog and bindless material PS: 10 refused shaders now translate, 18 read runtime values as 0); root UAV/SRV buffers;
#    f64 ops. A revision bump also retries the shaders listed in failed.txt.
# 4: s_buffer_load_dwordxN whose offset register is one of its own destinations read the later dwords at the new
#    value (material sampler index = garbage -> character faces black, wrong tree textures).
TRANSLATOR_REV = 4
RETRANSLATE = {2: ('vs', 'tess'), 3: ('vs', 'ps', 'cs'), 4: ('vs', 'ps', 'cs')}


def say(*a):
    print(*a, flush=True)


def gcn2hlsl_cli(src, out):
    import gcn2hlsl as T
    txt, todo = T.translate(src)
    open(out, 'w', encoding='utf-8').write(txt)
    for pc, t in todo[:25]: print('  ', pc, t, file=sys.stderr)
    return 0


def main(argv):
    if len(argv) >= 4 and argv[1] == '--gcn2hlsl':
        return gcn2hlsl_cli(argv[2], argv[3])
    if len(argv) < 2:
        say(__doc__); return 2
    game = os.path.abspath(argv[1])
    version = argv[argv.index('--version') + 1] if '--version' in argv else 'dev'
    shaders = os.path.join(game, 'shaders'); tr = os.path.join(shaders, 'translated')
    if not os.path.isdir(shaders):
        say('DONE 0 0'); return 0
    os.makedirs(tr, exist_ok=True)
    os.environ['NOCHECK'] = '1'  # compile_cso validates (and removes) what does not compile
    t0 = time.time()

    import build_shader_cache as B, tess_bypass, compile_cso
    flag = os.path.join(tr, compile_cso.CANCEL_FLAG)  # created by the launcher on "Pular"
    if os.path.exists(flag): os.remove(flag)
    cancelled = lambda: os.path.exists(flag)
    patches = B._patches()
    skip_path = os.path.join(tr, 'skip.txt')
    if not os.path.exists(skip_path) and patches.get('_cs_skip'):
        open(skip_path, 'w').write('\n'.join(patches['_cs_skip']) + '\n')

    failed_path = os.path.join(tr, 'failed.txt')
    failed = set()
    if os.path.exists(failed_path):
        lines = open(failed_path).read().split()
        if lines[:2] == ['#version', version]: failed = set(lines[2:])

    # Translator revision: when a fix changes the output for shaders already translated, the kinds listed for
    # the new revisions are translated again; only shaders whose HLSL changed are rewritten (and recompiled).
    rev_path = os.path.join(tr, 'translator_rev.txt')
    try: old_rev = int(open(rev_path).read().split()[0])
    except Exception: old_rev = 1
    redo = sorted({k for r, ks in RETRANSLATE.items() if old_rev < r <= TRANSLATOR_REV for k in ks})
    if redo: failed = set()   # a translator fix may make them work: try them again
    if redo and not cancelled():
        for k in [k for k in redo if k != 'tess']:
            rs, rf, _ = B.build(shaders, skip_existing=False, skip=failed, progress=lambda d, t: say('PROGRESS 1 %d %d' % (d, t)), cancelled=cancelled,
                                pattern='pso_h*_%s.dxbc' % k, changed_only=True)
            say('retranslate %s (rev %d -> %d): %s' % (k, old_rev, TRANSLATOR_REV, dict(rs)))
        if 'tess' in redo:
            tok, tfail = tess_bypass.run(game, skip_existing=False, log=lambda m: None, cancelled=cancelled, changed_only=True)
            say('retranslate tess: ok=%d fail=%d' % (tok, tfail))
    stat, new_failed, reasons = B.build(shaders, skip_existing=True, skip=failed, progress=lambda d, t: say('PROGRESS 1 %d %d' % (d, t)), cancelled=cancelled,
                                     pattern='pso_h*_*.dxbc')  # current bridge naming (older dev dumps are ignored)
    new_ok = sum(n for k, n in stat.items() if k.endswith(' ok'))
    say('translate: %s' % dict(stat))
    for r, n in reasons.most_common(5): say('  %d %s' % (n, r))
    say('PROGRESS 2 0 1')
    tok, tfail = tess_bypass.run(game, skip_existing=True, log=lambda m: None, cancelled=cancelled)
    say('tessellation: ok=%d fail=%d' % (tok, tfail))
    c, removed = compile_cso.run(tr, progress=lambda d, t: say('PROGRESS 3 %d %d' % (d, t)), log=lambda m: None, cancelled=cancelled, fast=True)
    say('compile: %s' % dict(c))
    if cancelled():  # leave failed.txt alone: the next run picks up where this one stopped
        os.remove(flag); say('CANCELLED'); return 1
    failed |= new_failed | set(removed)
    open(failed_path, 'w').write('#version %s\n' % version + '\n'.join(sorted(failed)) + '\n')
    if '--optimize' in argv:
        c2, _ = compile_cso.run(tr, progress=lambda d, t: say('PROGRESS 4 %d %d' % (d, t)), log=lambda m: None, cancelled=cancelled, optimize=True)
        say('optimize: %s' % dict(c2))
        if cancelled(): os.remove(flag); say('CANCELLED'); return 1
    if not cancelled(): open(rev_path, 'w').write('%d\n' % TRANSLATOR_REV)
    say('time %.1f s' % (time.time() - t0))
    say('DONE %d %d' % (new_ok - len(removed), len(new_failed) + len(removed)))
    return 0


if __name__ == '__main__':
    import multiprocessing
    multiprocessing.freeze_support()  # compile_cso uses worker processes (also inside the packaged exe)
    sys.exit(main(sys.argv))
