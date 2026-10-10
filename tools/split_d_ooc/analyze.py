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
    routable = re.search(r'# of routable nets\.+\s*:\s*(\d+)', route)
    errors = re.search(r'# of nets with routing errors\.+\s*:\s*(\d+)', route)
    if not routed or not routable or not errors:
        raise ValueError('Missing route status counts')
    drc = (directory/'drc.rpt').read_text()
    drc_issues = re.findall(r'^\|\s*([^|]+?)\s*\|\s*(Error|Critical Warning|Warning|Advisory)\s*\|\s*[^|]*\|\s*(\d+)\s*\|', drc, re.M)
    drc_counts = Counter()
    for _, severity, count in drc_issues:
        drc_counts[severity] += int(count)
    result = {'timing': metrics, 'check_timing': checks,
              'fully_routed_nets': int(routed.group(1)),
              'routable_nets': int(routable.group(1)),
              'routing_errors': int(errors.group(1)),
              'drc_counts': dict(drc_counts)}
    utilization = (directory/'utilization.rpt').read_text()
    top = re.search(r'^\|\s*fsa_stream_split_d\s*\|([^\n]+)', utilization, re.M)
    if not top:
        raise ValueError('Missing top hierarchy utilization')
    columns = [value.strip() for value in top.group(1).split('|')]
    result['resources'] = dict(zip(('LUT', 'logic_LUT', 'LUTRAM', 'SRL', 'FF', 'RAMB36', 'RAMB18', 'URAM', 'DSP'),
                                  map(int, columns[1:10])))
    result['DSP_gate'] = result['resources']['DSP']==40
    result['route_gate'] = result['routing_errors']==0 and result['fully_routed_nets']==result['routable_nets']
    for kind in ('setup', 'hold'):
        path = (directory/f'{kind}_paths.rpt').read_text()
        data = re.search(r'Data Path Delay:\s*([\d.]+)ns\s*\(logic ([\d.]+)ns.*?route ([\d.]+)ns', path)
        result[f'worst_{kind}_path'] = {
            'source': re.search(r'Source:\s*([^\n]+)', path).group(1).strip(),
            'destination': re.search(r'Destination:\s*([^\n]+)', path).group(1).strip(),
            'data_ns': float(data.group(1)), 'logic_ns': float(data.group(2)), 'route_ns': float(data.group(3))}
    diagnostic = next((directory/name for name in ('diagnostic_retry', 'diagnostic') if (directory/name).is_dir()), directory)
    result['diagnostic_source'] = str(diagnostic.relative_to(directory))
    properties = diagnostic/'context_properties.txt'
    if properties.exists():
        result['context'] = dict(line.split('=', 1) for line in properties.read_text().splitlines())
    for kind in ('hold', 'setup'):
        p = diagnostic/f'internal_{kind}.rpt'
        if p.exists():
            result[f'internal_{kind}_slack'] = float(re.search(r'Slack \([^)]*\)\s*:\s*(-?\d+\.\d+)ns', p.read_text()).group(1))
    violations = diagnostic/'hold_violators.tsv'
    if violations.exists():
        rows = list(csv.DictReader(violations.open(), delimiter='\t'))
        result['hold_violator_count'] = len(rows)
        result['hold_startpoint_groups'] = dict(Counter('internal' if '/' in row['source'] else row['source'].split('[')[0] for row in rows))
    result['hold_classification_complete_gate'] = result.get('hold_violator_count', -1)==metrics['hold_failing_endpoints']
    result['timing_and_route_gate'] = (metrics['wns']>=0 and metrics['tns']==0
        and metrics['whs']>=0 and metrics['ths']==0 and metrics['tpws']==0
        and result['route_gate'] and len(checks)==12 and all(v==0 for v in checks.values()))
    result['drc_error_gate'] = not (drc_counts['Error'] or drc_counts['Critical Warning'])
    constraints = (directory/'effective_constraints.xdc').read_text()
    context = result.get('context', {})
    result['ooc_model_gate'] = (context.get('clock_source')=='BUFGCE_X0Y48'
        and context.get('clock_period')=='10.000' and context.get('black_boxes')=='0'
        and re.search(r'set_clock_uncertainty -setup 2\.700 ', constraints) is not None
        and all(re.search(rf'set_{direction}_delay -clock ap_clk -{kind} {value} ', constraints)
                for direction in ('input', 'output') for kind, value in (('max', r'2\.000'), ('min', r'0\.000')))
        and not re.search(r'^\s*set_(?:false_path|multicycle_path)\b', constraints, re.M))
    result['full_ooc_gate'] = all(result[key] for key in ('ooc_model_gate', 'timing_and_route_gate', 'drc_error_gate', 'DSP_gate'))
    # User's stage4 scope defers boundary hold to full-system integration.
    # Keep the original full gate false when boundary timing fails.
    result['boundary_hold_status'] = 'deferred_to_system_integration' if metrics['whs']<0 else 'passes_declared_ooc_model'
    result['internal_ooc_gate'] = (result['ooc_model_gate'] and result['drc_error_gate']
        and result['DSP_gate'] and result.get('internal_setup_slack', -1)>=0
        and result.get('internal_hold_slack', -1)>=0 and metrics['wns']>=0
        and metrics['tns']==0 and metrics['tpws']==0 and result['route_gate']
        and len(checks)==12 and all(v==0 for v in checks.values())
        and result['hold_classification_complete_gate'] and result['hold_startpoint_groups'].get('internal', 0)==0)
    result['sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in directory.iterdir() if p.is_file() and p.suffix in ('.rpt', '.tsv', '.txt', '.xdc') and 'stdout' not in p.name}
    if diagnostic!=directory:
        result['sha256'].update({p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in diagnostic.iterdir() if p.is_file() and p.suffix in ('.rpt', '.tsv', '.txt', '.xdc')})
    return result


def check_regions(directory, plan_file):
    plan = json.loads(plan_file.read_text())['workers']
    constraints = (directory/'effective_constraints.xdc').read_text()
    actual = {}
    hard = {}
    expansion_ok = True
    for index in (0, 1):
        name = f'query_block_{index}'
        worker = f'worker{index}'
        hard[worker] = re.search(rf'set_property IS_SOFT (?:FALSE|0) \[get_pblocks {name}\]', constraints) is not None
        boxes = {}
        for ranges in re.findall(rf'resize_pblock \[get_pblocks {name}\] -add \{{([^}}]+)\}}', constraints):
            for typ, x0, y0, x1, y1 in re.findall(r'(SLICE|DSP48E2|RAMB18|RAMB36)_X(\d+)Y(\d+):\1_X(\d+)Y(\d+)', ranges):
                if typ in boxes:
                    raise ValueError('Multiple boxes for a resource type require explicit review')
                boxes[typ] = list(map(int, (x0, y0, x1, y1)))
        actual[worker] = boxes
        seed = plan[worker]['bounds']
        expansion_ok &= set(seed)==set(boxes)
        for typ, box in seed.items():
            effective = boxes.get(typ)
            # Vivado aligns DSP/RAMB ranges to two-site tiles. Retain seed
            # coordinates and accept only the observed one-site upper Y expansion.
            expansion_ok &= effective is not None and effective[:3]==box[:3] and (
                effective[3]==box[3] or (typ in ('DSP48E2', 'RAMB18') and box[3]%2==0 and effective[3]==box[3]+1))
    rows = list(csv.DictReader((directory/'primitive_locations.tsv').open(), delimiter='\t'))
    counts = Counter()
    outside = []
    for row in rows:
        match = re.search(r'/runQueryBlock_([01])_U0/', row['cell'])
        if not match or not row['site']:
            continue
        worker = 'worker'+match.group(1)
        site = re.fullmatch(r'(SLICE|DSP48E2|RAMB18|RAMB36)_X(\d+)Y(\d+)', row['site'])
        if not site:
            continue
        counts[worker] += 1
        typ, x, y = site.group(1), int(site.group(2)), int(site.group(3))
        box = actual[worker].get(typ)
        if not box or not (box[0]<=x<=box[2] and box[1]<=y<=box[3]) or row['slr']!='SLR0':
            outside.append(row)
    return {'checked_placed_primitives': dict(counts), 'outside': outside,
            'hard_properties': hard, 'seed_bounds': {worker: value['bounds'] for worker, value in plan.items()},
            'effective_bounds': actual, 'tile_expansion_gate': bool(expansion_ok),
            'gate': bool(counts['worker0'] and counts['worker1'] and not outside and expansion_ok and all(hard.values()))}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--regions-plan', type=Path)
    args = parser.parse_args()
    data = analyze(args.directory)
    if args.regions_plan:
        data['region_placement'] = check_regions(args.directory, args.regions_plan)
        data['full_ooc_gate'] = data['full_ooc_gate'] and data['region_placement']['gate']
        data['internal_ooc_gate'] = data['internal_ooc_gate'] and data['region_placement']['gate']
    encoded = json.dumps(data, indent=2, ensure_ascii=False)+'\n'
    if args.output:
        args.output.write_text(encoded, encoding='utf-8')
    print(encoded)
