"""Extract physical gates from archived Vivado reports, without changing them."""
import argparse
import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path


def analyze(directory):
    report = (directory/'timing_summary.rpt').read_text()
    summary = report.split('Design Timing Summary', 1)[1]
    match = re.search(r'^\s*((?:-?\d+(?:\.\d+)?\s+){11}-?\d+(?:\.\d+)?)\s*$', summary, re.M)
    if not match:
        raise ValueError('Missing timing summary table')
    fields = ('wns', 'tns', 'setup_failing_endpoints', 'setup_total_endpoints',
              'whs', 'ths', 'hold_failing_endpoints', 'hold_total_endpoints',
              'wpws', 'tpws', 'pulse_failing_endpoints', 'pulse_total_endpoints')
    numbers = match.group(1).split()
    metrics = {key: float(value) if key in ('wns', 'tns', 'whs', 'ths', 'wpws', 'tpws') else int(value)
               for key, value in zip(fields, numbers)}
    checks = {name: int(count) for name, count in re.findall(r'checking ([a-z_]+) \((\d+)\)', report)}
    route = (directory/'route_status.rpt').read_text()
    routed = re.search(r'# of fully routed nets\.+\s*:\s*(\d+)', route)
    errors = re.search(r'# of nets with routing errors\.+\s*:\s*(\d+)', route)
    if not routed or not errors:
        raise ValueError('Missing route status counts')
    drc = (directory/'drc.rpt').read_text()
    drc_issues = re.findall(r'^\|\s*([^|]+?)\s*\|\s*(Error|Critical Warning|Warning|Advisory)\s*\|\s*[^|]*\|\s*(\d+)\s*\|', drc, re.M)
    drc_counts = Counter()
    for _, severity, count in drc_issues:
        drc_counts[severity] += int(count)
    result = {'timing': metrics, 'check_timing': checks,
              'fully_routed_nets': int(routed.group(1)),
              'routing_errors': int(errors.group(1)),
              'drc_counts': dict(drc_counts)}
    properties = directory/'context_properties.txt'
    if properties.exists():
        result['context'] = dict(line.split('=', 1) for line in properties.read_text().splitlines())
    for kind in ('hold', 'setup'):
        p = directory/f'internal_{kind}.rpt'
        if p.exists():
            result[f'internal_{kind}_slack'] = float(re.search(r'Slack \([^)]*\)\s*:\s*(-?\d+\.\d+)ns', p.read_text()).group(1))
    violations = directory/'hold_violators.tsv'
    if violations.exists():
        rows = list(csv.DictReader(violations.open(), delimiter='\t'))
        result['hold_violator_count'] = len(rows)
        result['hold_startpoint_groups'] = dict(Counter('internal' if '/' in row['source'] else row['source'].split('[')[0] for row in rows))
    result['timing_and_route_gate'] = (metrics['wns']>=0 and metrics['tns']==0
        and metrics['whs']>=0 and metrics['ths']==0 and metrics['tpws']==0
        and result['routing_errors']==0 and len(checks)==12 and all(v==0 for v in checks.values()))
    result['drc_error_gate'] = not (drc_counts['Error'] or drc_counts['Critical Warning'])
    result['sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in directory.iterdir() if p.is_file() and p.suffix in ('.rpt', '.tsv', '.txt', '.xdc') and 'stdout' not in p.name}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data = analyze(args.directory)
    encoded = json.dumps(data, indent=2, ensure_ascii=False)+'\n'
    if args.output:
        args.output.write_text(encoded, encoding='utf-8')
    print(encoded)
