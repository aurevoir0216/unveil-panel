"""Bounded, isolated scratch data for one-shot device tests; no real jobs are deleted."""
from pathlib import Path
import json,shutil,time,threading

TEST_TTL_SECONDS=15*60
MARKER='.unveil-test.json'
_guard=threading.RLock()

def mark_test_run(run,finished=False,now=None):
    now=time.time() if now is None else now
    run=Path(run)
    with _guard:
        marker=run/MARKER
        try:value=json.loads(marker.read_text())
        except (OSError,ValueError):value={'test':True,'created_at':now}
        value.update(test=True,expires_at=now+TEST_TTL_SECONDS)
        if finished:value['finished_at']=now
        temporary=run/(MARKER+'.tmp')
        temporary.write_text(json.dumps(value));temporary.replace(marker)
    return value

def cleanup_test_runs(root,active_run=None,force=False,now=None):
    now=time.time() if now is None else now
    base=Path(root)/'unveil_test'
    report={'removed':[],'skipped':[],'ttl_seconds':TEST_TTL_SECONDS}
    if not base.exists():return report
    if base.is_symlink():
        report['skipped'].append('symlink-test-root');return report
    active=Path(active_run).resolve() if active_run else None
    with _guard:
        parent=base.resolve()
        for run in base.glob('run_*'):
            # The namespace and direct parent are the deletion boundary. Symlinks
            # are never followed; real jobs and original hard-link sources live elsewhere.
            if run.is_symlink() or not run.is_dir() or run.resolve().parent!=parent:
                report['skipped'].append(run.name);continue
            if active is not None and run.resolve()==active:
                report['skipped'].append(run.name);continue
            try:
                try:expires=float(json.loads((run/MARKER).read_text())['expires_at'])
                except (OSError,ValueError,KeyError,TypeError):expires=run.stat().st_mtime+TEST_TTL_SECONDS
                if force or expires<=now:
                    shutil.rmtree(run);report['removed'].append(run.name)
            except FileNotFoundError:continue
    return report
