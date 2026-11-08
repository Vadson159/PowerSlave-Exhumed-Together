"""
Builds a standalone self-contained HTML tracker that can be double-clicked in Windows Explorer.
Embeds the latest parsed state from the 4 Markdown files directly into the HTML file.
"""

import os
import json
from tracker_parser import parse_all_4_files

def build_standalone(output_paths=None):
    if output_paths is None:
        output_paths = [
            os.path.abspath('PowerSlave_Exhumed_Together_Roadmap_Tracker.html'),
            os.path.abspath('docs/PowerSlave_Exhumed_Together_Roadmap_Tracker.html')
        ]

    # Parse 4 files
    data = parse_all_4_files()

    # Read template
    template_path = os.path.join(os.path.dirname(__file__), 'template.html')
    with open(template_path, 'r', encoding='utf-8') as f:
        html = f.read()

    # Inject data into template
    data_json = json.dumps(data, ensure_ascii=False, indent=2)
    injection = f"window.__INITIAL_DATA__ = {data_json};"
    html = html.replace('window.__INITIAL_DATA__ = null;', injection)

    for out_p in output_paths:
        os.makedirs(os.path.dirname(out_p), exist_ok=True)
        with open(out_p, 'w', encoding='utf-8') as f:
            f.write(html)
        print(f"Generated standalone HTML: {out_p} ({len(html)} bytes)")

if __name__ == '__main__':
    build_standalone()
