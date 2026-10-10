"""Generate two coarse worker regions from an actual Vivado location report."""
import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('locations', type=Path)
parser.add_argument('tcl', type=Path)
parser.add_argument('plan', type=Path)
args = parser.parse_args()
rows = [line.split('\t') for line in args.locations.read_text().splitlines()[1:]]
assert all(len(row)==4 for row in rows), 'Invalid location TSV'
summary = {'basis': str(args.locations),
           'basis_sha256': hashlib.sha256(args.locations.read_bytes()).hexdigest(),
           'workers': {}}
lines = ['# Exact occupied bounding boxes from the auto_context placement.',
         f'# primitive_locations.tsv SHA256: {summary["basis_sha256"]}',
         '# Hard placement regions, routing not contained, no forced SLR split.',
         '# Bounding boxes may overlap; this does not reserve separate slices.']
for block in (0, 1):
    selected = [row for row in rows if f'/runQueryBlock_{block}_U0/' in row[0]]
    assert selected, 'Missing worker hierarchy'
    slrs = Counter(row[3] for row in selected if row[2])
    assert set(slrs)=={'SLR0'}, 'Reconsider floorplan for non-SLR0 placement'
    bounds = defaultdict(list)
    for row in selected:
        for typ, x, y in re.findall(r'(SLICE|DSP48E2|RAMB18|RAMB36)_X(\d+)Y(\d+)', row[2]):
            bounds[typ].append((int(x), int(y)))
    boxes = {typ: [min(x for x,y in pos), min(y for x,y in pos),
                   max(x for x,y in pos), max(y for x,y in pos)]
             for typ, pos in bounds.items()}
    summary['workers'][f'worker{block}'] = {'primitive_count': len(selected),
                                         'SLR_counts': dict(slrs), 'bounds': boxes}
    regions = ' '.join(f'{typ}_X{x0}Y{y0}:{typ}_X{x1}Y{y1}'
                       for typ, (x0, y0, x1, y1) in boxes.items())
    lines += [f'set worker [get_cells -hier -filter {{NAME =~ */runQueryBlock_{block}_U0}}]',
              'if {[llength $worker] != 1} { error "Worker hierarchy mismatch" }',
              f'create_pblock query_block_{block}',
              f'add_cells_to_pblock [get_pblocks query_block_{block}] $worker',
              f'resize_pblock [get_pblocks query_block_{block}] -add {{{regions}}}',
              f'set_property CONTAIN_ROUTING 0 [get_pblocks query_block_{block}]',
              f'set_property IS_SOFT 0 [get_pblocks query_block_{block}]',
              f'if {{[get_property IS_SOFT [get_pblocks query_block_{block}]] != 0}} {{ error "Hard Pblock not applied" }}']
args.tcl.write_text('\n'.join(lines)+'\n', encoding='utf-8')
args.plan.write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
print(json.dumps(summary))
